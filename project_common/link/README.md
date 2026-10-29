# UART frame link

The HdlcParser HSM is imported from sm_tracer_tui:
`application/aos/sp_mngr/hdlc_parser.c` and `hdlc_parser_priv.h`.
Source revision: `fa22dcdc03eb443a219193c1f296d92bf68d2048`.
The original copyright/license is preserved. Local changes replace desktop
port/DBC includes with standalone sm_assert, use the first-member cast,
rename the header and wrap declarations. State hierarchy and wire format
are unchanged. The imported parser tests are retained under tests/hdlc.

Frame: flag 7E, escaped sequence/id/length/payload/checksum, flag 7E.
Checksum makes the unescaped byte sum FF; 7E/7D escape with 7D and XOR 20.
Payload length is 8 bits. F0/F1 identify control requests/replies and F2
identifies boot text. Existing tracer IDs identify diagnostics. Raw asset
bytes follow a valid block header without escaping or fragmentation.
The bootloader transfer HSM owns control/data/resync contexts; the parser
is invoked only in control. See `docs/uart-link.md` for the complete flow.
