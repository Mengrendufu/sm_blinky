#!/usr/bin/env python3
"""Native NVM tests only; no firmware configure/build or hardware access."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
work = Path(tempfile.mkdtemp(prefix="nvm-io-test-"))
includes = ["project_common/flash", "project_common/assets",
            "tests/ui_components/ports", "app/application/aos/flash/nvm",
            "app/application/nvm", "app/ports",
            "3rd_party/sm_nvs/include", "3rd_party/sm_nvs/src",
            "3rd_party/sm_hsm/include"]
command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
           "-fsanitize=address,undefined", "-fno-pie", "-no-pie"]
for folder in includes:
    command += ["-I", str(ROOT / folder)]
sources = ["app/application/aos/flash/nvm/nvm_manager.c",
           "3rd_party/sm_nvs/src/nvm_meta.c",
           "app/application/nvm/instance/nvm_boot.c",
           "app/application/nvm/instance/nvm_blinky.c"]
command += [str(ROOT / source) for source in [
    "tests/nvm/test_nvm.c", "tests/nvm/trace_stub.c", *sources]]
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
includes += ["app/application", "app/application/ui",
             "app/application/ui/hsm",
             "app/application/ui/thread",
             "app/application/aos/flash",
             "app/application/aos/flash/hsm", "app/application/aos/blinky",
             "app/bsp/devices", "app/ports/static_pool",
             "3rd_party/common_c/include",
             "3rd_party/sm_hsm/include", "3rd_party/sm_sst/include"]
for folder in includes:
    command += ["-I", str(ROOT / folder)]
command += [str(ROOT / source) for source in [
    "tests/nvm/test_flash_events.c",
    "app/application/aos/flash/hsm/sm_flash.c",
    "3rd_party/sm_hsm/src/sm_hsm.c", "app/ports/static_pool/sst_evt_pool.c",
    "app/application/ui/thread/ui_evt.c",
    "3rd_party/sm_sst/sst_c/src/sst_pubsub.c",
    "3rd_party/common_c/src/static_pool/static_pool.c"]]
command += ["-o", str(work / "events")]
subprocess.run(command, check=True)
subprocess.run([str(work / "events")], check=True)
subprocess.run([str(work / "events"), "0"], check=True)

# A structured block uses the public registration API directly.
command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
           "-fsanitize=address,undefined", "-fno-pie", "-no-pie"]
for folder in includes:
    command += ["-I", str(ROOT / folder)]
command += [str(ROOT / source) for source in [
    "tests/nvm/test_typed.c", "tests/nvm/trace_stub.c", *sources]]
command += ["-o", str(work / "typed-test")]
subprocess.run(command, check=True)
for scenario in range(3):
    subprocess.run([str(work / "typed-test"), str(scenario)], check=True)
