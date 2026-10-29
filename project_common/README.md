# Project common

Project-local sources and contracts for the App and bootloader images.
This directory belongs to the template repository; it is not a separate
library repository or a submodule.

| Directory | Shared content |
|---|---|
| `flash/` | SPI2 NOR implementation, public methods and device geometry |
| `assets/` | AST2 format and NOR partition/slot topology |
| `fault/` | Retained record layout/seal and stack-free Cortex-M3 capture |
| `startup/` | Image addresses, upgrade flag and reserved no-init RAM |
| `link/` | Imported HDLC decoder HSM, encoder and control-message IDs |

`project_common` is a CMake INTERFACE target. Its C/assembly sources are
compiled separately into each image using that image's includes and flags.
Waiting adapters and board initialization remain with each image.
The NOR driver is moved verbatim; core/port refactoring is a separate step.

Both linker scripts include `startup/project_layout.h`. CMake preprocesses
them into their build directories, so C and GNU ld use one set of constants.
Changing the source linker script or layout header triggers reconfiguration.
Use the generated linker script when invoking GNU ld directly.

Generic CMSIS startup code continues to come from the root submodule.
The no-init reservation survives only resets that retain SRAM contents;
it is not nonvolatile storage. Bootloader owns persistence/reporting in
`bootloader/diagnostics`; its two Flash pages are reserved in the shared
layout and excluded from App code placement.
