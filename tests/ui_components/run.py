#!/usr/bin/env python3
"""Run native LVGL/UI integration with ASan/UBSan; never build firmware.

Requires native gcc, CMake, Ninja and checked-out project submodules.
Artifacts and the generated native build stay in a unique /tmp directory.
"""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    work = Path(tempfile.mkdtemp(prefix="ui-components-test-"))
    config = (ROOT / "bsp/lvgl/lv_conf.h").read_text()
    # Native objects have 64-bit pointers. This pool is test-only; it does
    # not alter firmware configuration or establish the MCU memory budget.
    config = config.replace("(24U * 1024U)", "(64U * 1024U)")
    (work / "lv_conf_host.h").write_text(config)
    includes = [HERE / "ports"] + [ROOT / path for path in [
        "application", "application/ui", "application/ui/thread",
        "application/ui/hsm", "application/aos/flash",
        "application/aos/flash/hsm", "application/nvm",
        "ports/common_c", "ports/static_pool", "bsp/devices", "ports",
        "3rd_party/sm_nvs/include", "3rd_party/sm_nvs/src",
        "3rd_party/common_c/include", "3rd_party/sm_hsm/include",
        "3rd_party/sm_sst/include", "3rd_party/lvgl", "3rd_party/lvgl/src",
    ]]
    sources = [HERE / "test_ui_components.c"] + [ROOT / path for path in [
        "application/ui/hsm/sm_ui.c", "application/ui/thread/ui_evt.c",
        "application/nvm/nvm.c", "3rd_party/common_c/src/time_evt/time_evt.c",
        "3rd_party/sm_hsm/src/sm_hsm.c",
        "ports/static_pool/sst_evt_pool.c",
        "3rd_party/common_c/src/static_pool/static_pool.c",
    ]]
    for folder in ["metrics", "widgets"]:
        sources += sorted((ROOT / "application/ui/hsm" / folder).glob("*.c"))
    quote = lambda paths: " ".join(f'"{p}"' for p in paths)
    cmake = f'''cmake_minimum_required(VERSION 3.25)
project(ui_host C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-O1 -g -fsanitize=address,undefined -fno-pie")
set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=address,undefined -no-pie \
-Wl,--defsym,_ebss=0x2000E000,--defsym,_estack=0x2000FFE0")
include_directories({quote(includes)})
add_compile_definitions(LV_CONF_PATH="{work}/lv_conf_host.h")
add_compile_definitions(UI_TEST_ROOT="{ROOT}")
file(GLOB_RECURSE LVGL "{ROOT}/3rd_party/lvgl/src/*.c")
add_library(lvgl STATIC ${{LVGL}})
target_compile_options(lvgl PRIVATE -w)
add_executable(test {quote(sources)})
target_link_libraries(test PRIVATE lvgl m)
target_compile_options(test PRIVATE -Wall -Wextra -Werror)
'''
    (work / "CMakeLists.txt").write_text(cmake)
    print(f"Native UI test artifacts: {work}", flush=True)
    commands = [
        ["cmake", "-S", str(work), "-B", str(work / "build"), "-G", "Ninja"],
        ["cmake", "--build", str(work / "build"), "-j", "8"],
    ]
    for number, command in enumerate(commands):
        with (work / f"build-{number}.log").open("w") as log:
            subprocess.run(command, check=True, stdout=log,
                           stderr=subprocess.STDOUT)
    rows = []
    for case in range(6):
        result = subprocess.run([str(work / "build/test"), str(case)],
                                cwd=work, capture_output=True, text=True)
        rows.append({"case": case, "code": result.returncode,
                     "stdout": result.stdout, "stderr": result.stderr})
        print(result.stdout.strip() if result.returncode == 0
              else result.stderr)
    (work / "results.json").write_text(json.dumps(rows, indent=2))
    assert all(row["code"] == 0 for row in rows), work


if __name__ == "__main__":
    main()
