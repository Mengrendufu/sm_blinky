# Bootloader

Bare-metal image within the sm_blinky template repository.

- `application/bl_main.c`: platform setup, callbacks and boot entry.
- `application/bl_boot.c/.h`: stage/transfer HSMs and App handover.
- `link/`: shared blocking HDLC transmitter for replies and logs.
- `ports/`: standalone sm_hsm adaptation, without SST/FreeRTOS.
- `diagnostics/`: retained-snapshot persistence and framed UART reporting.
- `bsp/platform/`: bootloader-specific MSP, HAL timebase and interrupts.
- `../project_common/`: Flash driver, asset contracts and fault capture.
- `../3rd_party/`: shared upstream submodules.

Build both images from the repository root, or select `build-bootloader`.
This directory also has a preset for a separate `../build/bootloader` build.
From the repository root, `tools/flash.sh` builds/programs both images;
`bootloader/tools/flash.sh` builds/programs bootloader only.
Only build or program when explicitly requested.

Bootloader occupies the first 16 KB = 16,384 B of internal Flash.
The App entry and no-init RAM layout come from
`../project_common/startup/project_layout.h`.

After UART initialization and before upgrade-mode selection, bootloader
handles the RAM snapshot and reports the latest saved fault. The journal
occupies `0x0807F000..0x0807FFFF`: two 2 KB = 2,048 B pages. It uses append
writes and page rotation; full-snapshot duplicates of the latest entry are
skipped. UART output and normal restarts never erase the saved history.
If this boot finds a valid no-init snapshot, it reports once and stays here,
before upgrade-mode selection or App handover, even if saving or UART fails.
This is the future OTA rollback trigger. Empty retained RAM skips Flash
lookup and fault output. Invalid nonempty RAM retains diagnostic/history
replay without blocking handover. A successful save clears RAM, so another
reset may boot silently with respect to retained fault records.
See `../docs/fault-record.md` for failure handling and the wire format.

Control commands/replies use HDLC; asset bytes remain raw and length-bound.
Without an upgrade flag, a fixed 200 ms polling window accepts a complete
valid HELLO. Success enters burn mode and replies after NOR initialization;
timeout continues App startup. Noise and bad frames do not extend the window.
The boot-stage HSM owns command routing, asset entry, App handover and fault
or invalid-App holds. Future IAP/OTA commands can select their own stages;
those commands and operations are not implemented.
The C decoder HSM is imported from sm_tracer_tui. Handshake and transfer
contexts each own a parser; raw asset data bypasses decoding.
See `../docs/uart-link.md` for protocol details and board verification.

On 2026-10-29, both images linked and passed programming/readback checks.
Bootloader uses 15,996 B of Flash, leaving 388 B in its 16 KB partition.
Do not infer room for IAP/OTA from the current placeholder; recheck size
before adding those implementations. App is back in normal operation.
