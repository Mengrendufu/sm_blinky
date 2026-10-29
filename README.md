# sm_blinky

STM32F103ZET6 firmware template in one repository.

| Directory | Responsibility |
|---|---|
| `app/` | FreeRTOS, SST AOs, UI/HSM, NVM and App-specific BSP/ports |
| `bootloader/` | Boot stages, assets, diagnostics and App handover |
| `project_common/` | Project-local driver, asset and diagnostic contracts |
| `3rd_party/` | Pinned upstream submodules shared by both builds |
| `tests/`, `tools/`, `docs/` | Validation, programming and documentation |

## Checkout

```sh
git clone --recurse-submodules <repository-url>
# Existing checkout:
git submodule update --init --recursive
```

## Build

Arm GNU Toolchain, CMake and Ninja must be available on PATH.
Run builds and programming only when explicitly requested.

```sh
cmake --preset debug
cmake --build --preset build-debug       # both images
cmake --build --preset build-app         # App only
cmake --build --preset build-bootloader  # bootloader only
```

Combined-build artifacts stay in `build/debug/`: `sm_blinky.elf/.hex/.map`
and `bootloader.elf/.hex/.map`. Each image also has its own source-directory
preset for separate configuration, using `build/app` or `build/bootloader`.

`tools/flash.sh` builds and programs both images. `-nobuild` skips building.
`bootloader/tools/flash.sh` programs only bootloader, using the root build.

## Shared project contracts

Both images directly reference the same files in `project_common`.
There is no separate shared repository or new submodule for these files.

- `flash/`: existing SPI2 NOR driver and device geometry, unchanged.
- `assets/`: NOR partition map and AST2 asset format.
- `fault/`: retained record format and stack-free fault-capture assembly.
- `startup/project_layout.h`: Flash entry addresses and reserved RAM layout.
- `link/`: HDLC decoder HSM, encoder and message IDs.

The startup source remains in the CMSIS Device submodule. Each image owns
its initialization and waiting adapters. Each linker script is preprocessed
from the shared layout header during CMake configuration.

App faults capture into no-init RAM and reset. Bootloader validates and
persists the snapshot to the last 4 KB of internal Flash before reporting.
Exact duplicates of the latest snapshot cause no programming or erase.
App Flash is now 492 KB = 503,808 B; bootloader remains 16 KB = 16,384 B.
See `docs/fault-record.md` for the journal and validation boundaries.

UART control/replies and boot diagnostics use HDLC; asset payloads stay raw.
The boot-stage HSM selects handshake, assets, App handover or a stopped
fault/invalid-App stage. Asset transfer has its own control/data/resync HSM.
Without an upgrade flag, a valid HELLO within 200 ms selects assets; timeout
checks and starts App. IAP/OTA and rollback metadata remain unimplemented.
Uploads need the matching `tools/burn-asset.py`; see `docs/uart-link.md`.

## IDE and validation

Run `tools/ide_db.sh` from the repository root after source/CMake changes.
It configures without building and keeps App, bootloader and LVGL entries;
shared sources use App flags, with one deterministic entry per source.

```sh
python3 tests/test_asset_loop.py
python3 tests/hdlc/run.py
python3 tests/fault_log/run.py
```

The 2026-10-29 build/programming check used 15,996 B of bootloader Flash
(388 B free in 16 KB) and 199,180 B of App Flash. These are this build's
numbers; remeasure after changes. Board checks covered normal startup,
flag-free HELLO entry without asset writes, and App restoration. User
confirmed display, page switching and the Live arc.
Fault records use reserved IDs 1/2; assertion retains ID 0.
See `docs/fault-record.md` for actual fault-injection coverage and limits.
