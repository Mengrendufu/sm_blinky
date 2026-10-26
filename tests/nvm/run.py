#!/usr/bin/env python3
"""Native NVM tests only; no firmware configure/build or hardware access."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
work = Path(tempfile.mkdtemp(prefix="nvm-io-test-"))
includes = ["tests/ui_components/ports", "application/nvm", "ports",
            "3rd_party/sm_nvs/include", "3rd_party/sm_nvs/src",
            "3rd_party/sm_hsm/include"]
command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
           "-fsanitize=address,undefined", "-fno-pie", "-no-pie"]
for folder in includes:
    command += ["-I", str(ROOT / folder)]
command += [str(ROOT / source) for source in [
    "tests/nvm/test_nvm.c", "application/nvm/nvm.c",
    "tests/nvm/trace_stub.c"]]
command += ["-o", str(work / "test")]
print(f"NVM test artifacts: {work}", flush=True)
subprocess.run(command, check=True)
for scenario in range(5):
    subprocess.run([str(work / "test"), str(scenario)], check=True)

# Event routing uses the real Flash HSM, ingress, Blinky maintenance and GC.
(work / "extra.h").write_text(
    '#include "task.h"\n'
    'int xPortIsInsideInterrupt(void);\n'
    'TaskHandle_t xTaskGetCurrentTaskHandle(void);\n')
command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
           "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
           "-include", str(work / "extra.h")]
includes += ["application", "application/ui", "application/ui/hsm",
             "application/ui/thread",
             "application/aos/flash",
             "application/aos/flash/hsm", "application/aos/blinky",
             "bsp/devices", "ports/static_pool", "3rd_party/common_c/include",
             "3rd_party/sm_hsm/include", "3rd_party/sm_sst/include"]
for folder in includes:
    command += ["-I", str(ROOT / folder)]
command += [str(ROOT / source) for source in [
    "tests/nvm/test_flash_events.c", "application/aos/flash/hsm/sm_flash.c",
    "3rd_party/sm_hsm/src/sm_hsm.c", "ports/static_pool/sst_evt_pool.c",
    "application/ui/thread/ui_evt.c", "3rd_party/sm_sst/sst_c/src/sst_pubsub.c",
    "3rd_party/common_c/src/static_pool/static_pool.c"]]
command += ["-o", str(work / "events")]
subprocess.run(command, check=True)
subprocess.run([str(work / "events")], check=True)
subprocess.run([str(work / "events"), "0"], check=True)

# Register a real structured item only in the temporary test copy. This
# checks the extension path without adding a fake field to device firmware.
typed = work / "typed"
typed.mkdir()
header = (ROOT / "application/nvm/nvm.h").read_text()
header = header.replace("    NVM_ID_LIMIT", "    NVM_TEST_RECORD = 3U,\n"
                        "    NVM_ID_LIMIT")
header = header.replace("// Stable IDs:",
                        "typedef struct {\n    uint64_t flags;\n"
                        "    uint8_t payload[17];\n} Nvm_TestData;\n\n"
                        "// Stable IDs:")
(typed / "nvm.h").write_text(header)
source = (ROOT / "application/nvm/nvm.c").read_text()
source = source.replace("static Nvm_BlinkyData Nvm_blinky_;",
                        "static Nvm_BlinkyData Nvm_blinky_;\n"
                        "static Nvm_TestData Nvm_test_;")
source = source.replace("    Nvm_BlinkyData blinky;",
                        "    Nvm_BlinkyData blinky;\n"
                        "    Nvm_TestData record;")
source = source.replace("    [NVM_BOOT_COUNT] = {\n",
                        "    [NVM_TEST_RECORD] = {\n"
                        "        .data = &Nvm_test_, .size = sizeof(Nvm_test_)\n"
                        "    },\n    [NVM_BOOT_COUNT] = {\n")
source = source.replace("    [NVM_BOOT_COUNT] = -EAGAIN,",
                        "    [NVM_TEST_RECORD] = -EAGAIN,\n"
                        "    [NVM_BOOT_COUNT] = -EAGAIN,")
(typed / "nvm.c").write_text(source)
command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
           "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
           "-I", str(typed)]
for folder in includes:
    command += ["-I", str(ROOT / folder)]
command += [str(ROOT / "tests/nvm/test_typed.c"), str(typed / "nvm.c"),
            str(ROOT / "tests/nvm/trace_stub.c"),
            "-o", str(work / "typed-test")]
subprocess.run(command, check=True)
for scenario in range(3):
    subprocess.run([str(work / "typed-test"), str(scenario)], check=True)

