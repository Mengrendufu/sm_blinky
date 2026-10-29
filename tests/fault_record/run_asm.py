#!/usr/bin/env python3
"""Execute the isolated production assembly with a Cortex-M register model.

Requires arm-none-eabi tools and Python unicorn. No target is connected.
"""
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn import UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_R12, UC_ARM_REG_LR, UC_ARM_REG_MSP, UC_ARM_REG_PSP,
    UC_ARM_REG_CONTROL, UC_ARM_REG_PRIMASK, UC_ARM_REG_BASEPRI,
    UC_ARM_REG_FAULTMASK, UC_ARM_REG_IPSR,
)

ROOT = Path(__file__).resolve().parents[2]
WORK = Path(tempfile.mkdtemp(prefix="fault-asm-test-"))
BASE, RAM, STASH = 0x08004000, 0x20000000, 0x2000FF60


def run(*args):
    return subprocess.check_output(args, text=True)


# Preprocess and assemble only the isolated fault entry, not the firmware.
source = ROOT / "project_common/fault/bsp_fault_capture.S"
run("arm-none-eabi-gcc", "-E", "-P", "-x", "assembler-with-cpp",
    "-I", str(ROOT / "project_common/fault"), str(source),
    "-o", str(WORK / "entry.s"))
run("arm-none-eabi-as", "-mcpu=cortex-m3", "-mthumb",
    str(WORK / "entry.s"), "-o", str(WORK / "entry.o"))
(WORK / "stub.s").write_text(".global Reset_Handler\n"
                            '.thumb_func\nReset_Handler: b .\n'
                            '.section .note.GNU-stack, "", %progbits\n')
run("arm-none-eabi-as", "-mcpu=cortex-m3", "-mthumb",
    str(WORK / "stub.s"), "-o", str(WORK / "stub.o"))
# Exercise both real linker scripts and their retained-area reservation.
for owner, script in (
        (ROOT / "app", "bsp/platform/STM32F103ZETX_FLASH.ld"),
        (ROOT / "bootloader", "bsp/platform/stm32f1xx_bl.ld")):

    linker = WORK / (Path(script).stem + ".ld")
    run("arm-none-eabi-gcc", "-E", "-P", "-x", "c",
        "-I", str(ROOT / "project_common/startup"),
        str(owner / script), "-o", str(linker))
    elf = WORK / (Path(script).stem + ".elf")
    run("arm-none-eabi-gcc", "-nostdlib", "-mcpu=cortex-m3",
        "-mthumb", "-T", str(linker),
        str(WORK / "entry.o"), str(WORK / "stub.o"), "-o", str(elf))
    symbols = run("arm-none-eabi-nm", "-n", str(elf))
    for name, value in (("_sfault", STASH), ("_estack", STASH),
                        ("_snoinit", 0x2000FFE0)):
        assert re.search(rf"^{value:08x} . {name}$", symbols, re.M), name
    for handler in ("MemManage_Handler", "BusFault_Handler",
                    "UsageFault_Handler"):
        alias = re.search(rf"^(\w+) T {handler}$", symbols, re.M)
        hard = re.search(r"^(\w+) T HardFault_Handler$", symbols, re.M)
        assert alias and alias[1] == hard[1]
    sections = run("arm-none-eabi-readelf", "-SW", str(elf))
    assert re.search(r"\.fault_noinit\s+NOBITS\s+2000ff60", sections)
    if owner == ROOT / "app":
        run("arm-none-eabi-objcopy", "-O", "binary", "--only-section=.text",
            str(elf), str(WORK / "entry.bin"))
        dis = run("arm-none-eabi-objdump", "-d", str(elf))
        (WORK / "entry.dis").write_text(dis)
        assert not re.search(r"\b(push|pop|bl|blx)\b", dis)
        # Both entry points are in .text; get actual capture symbol address.
        entry = int(re.search(r"^(\w+) T HardFault_Handler$", symbols,
                              re.M)[1], 16)
        text_base = int(re.search(r"\.text\s+PROGBITS\s+(\w+)",
                                  sections)[1], 16)
code = (WORK / "entry.bin").read_bytes()
spin_addresses = [int(m[1], 16) for m in re.finditer(
    r"^\s*(\w+):.*\bsubs\s+r3, #1", dis, re.M)]
assert len(spin_addresses) == 2
core = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
        UC_ARM_REG_R12]


def capture(kind=3, exc_return=0xFFFFFFFD, sp=RAM + 0x1000,
            cfsr=0x10000, msp=RAM + 0x2000):
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    cpu.mem_map(BASE, 0x4000)
    cpu.mem_write(text_base, code)
    cpu.mem_map(RAM, 0x10000)
    cpu.mem_map(0xE000E000, 0x2000)
    cpu.mem_map(0x40010000, 0x1000)
    cpu.mem_write(STASH, bytes([0xA5]) * 128)
    cpu.mem_write(0x2000FFE0, struct.pack("<I", 0xB007B007))
    frame = [0x100 + i for i in range(8)]
    if RAM <= sp <= STASH - 32 and sp % 4 == 0:
        cpu.mem_write(sp, struct.pack("<8I", *frame))
    for i, reg in enumerate(core):
        cpu.reg_write(reg, 0xA000 + i)
    cpu.reg_write(UC_ARM_REG_IPSR, kind)
    cpu.reg_write(UC_ARM_REG_CONTROL, 0)
    cpu.reg_write(UC_ARM_REG_MSP, msp if exc_return & 4 else sp)
    cpu.reg_write(UC_ARM_REG_PSP, sp if exc_return & 4 else RAM + 0x3000)
    cpu.reg_write(UC_ARM_REG_LR, exc_return)
    cpu.reg_write(UC_ARM_REG_PRIMASK, 0)
    cpu.reg_write(UC_ARM_REG_BASEPRI, 0x40)
    cpu.reg_write(UC_ARM_REG_FAULTMASK, 0)
    for offset, value in ((0x28, cfsr), (0x2C, 0x40000000),
                          (0x34, 0x12345678), (0x38, 0xDEADBEEF),
                          (0x24, 0x70000), (0x0C, 0x300)):
        cpu.mem_write(0xE000ED00 + offset, struct.pack("<I", value))
    writes, reads, resets = [], [], []

    def write(cpu, access, address, size, value, _):
        writes.append((address, size, value))
        if address == 0xE000ED0C:
            resets.append(value)
            cpu.emu_stop()

    def read(cpu, access, address, size, value, _):
        if RAM <= address < STASH:
            reads.append(address)

    cpu.hook_add(UC_HOOK_MEM_WRITE, write)
    cpu.hook_add(UC_HOOK_MEM_READ, read)
    # Skip elapsed blink time only; execute the actual GPIO/reset path.
    def skip_spin(cpu, address, size, _):
        cpu.reg_write(UC_ARM_REG_R3, 1)

    for address in spin_addresses:
        cpu.hook_add(UC_HOOK_CODE, skip_spin, begin=address, end=address)
    cpu.emu_start(entry | 1, BASE + 0x4000, count=10000)
    assert resets == [0x05FA0304], resets
    record = bytes(cpu.mem_read(STASH, 124))
    words = struct.unpack("<31I", record)
    h = 0x46544C32
    for word in words[:-1]:
        h = ((h ^ word) * 16777619) & 0xFFFFFFFF
    assert words[-1] == (h or 0xFFFFFFFF)
    assert words[2:6] == (kind, cfsr, 0x40000000, 0xDEADBEEF)
    assert words[12:20] == tuple(0xA000 + i for i in range(4, 12))
    assert words[20:22] == (
        msp if exc_return & 4 else sp,
        sp if exc_return & 4 else RAM + 0x3000)
    assert words[22:29] == (exc_return, 0, 0, 0x40, 0,
                            0x12345678, 0x70000)
    assert cpu.mem_read(0x2000FFE0, 4) == struct.pack("<I", 0xB007B007)
    assert cpu.mem_read(STASH + 124, 4) == bytes([0xA5]) * 4
    retained = [(a, s, v) for a, s, v in writes if RAM <= a < RAM + 0x10000]
    assert all(STASH <= a and a + s <= STASH + 124
               for a, s, _ in retained), retained
    assert retained[0] == (STASH + 120, 4, 0)
    assert retained[-1][0] == STASH + 120
    if words[29]:
        assert words[29] == 1
        assert words[0:2] == (frame[6], frame[5])
        assert words[6:12] == tuple(frame[:5] + [frame[7]])
        assert len(reads) == 8
    else:
        assert not reads, reads
        assert words[0:2] == (0, 0) and words[6:12] == (0,) * 6
    return words, record


for kind in range(3, 7):
    for exc_return in (0xFFFFFFF1, 0xFFFFFFF9, 0xFFFFFFFD):
        words, record = capture(kind, exc_return)
        assert words[29] == 1
        if kind == 3 and exc_return == 0xFFFFFFFD:
            (WORK / "captured.bin").write_bytes(record)
for sp in (0, 0xFFFFFFFC, RAM - 4, STASH - 28, RAM + 0x1002):
    assert capture(sp=sp)[0][29] == 0
for status in (0x8, 0x10, 0x800, 0x1000):
    assert capture(cfsr=status)[0][29] == 0
assert capture(exc_return=0xFFFFFFF5)[0][29] == 0
assert capture(msp=0xDEADBEE0)[0][29] == 1  # PSP frame, broken MSP
assert capture(sp=STASH - 32)[0][29] == 1  # exact upper boundary
assert capture(sp=RAM)[0][29] == 1
print("PASS: isolated assembly, four faults, MSP/PSP, broken stacks,")
print("      frame guards, seal-last, reset, bounds, both linker layouts")
print(f"Assembly artifacts: {WORK}")
# Let the C consumer validate a record produced by the real assembly.
print(run("python3", str(Path(__file__).with_name("run.py")),
          str(WORK / "captured.bin")), end="")
