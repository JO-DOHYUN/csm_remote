# BOARD_ARCH_CURRENT_ADDENDUM_KO

## Purpose
This addendum is the current CSM architecture correction for stale older notes in
`BOARD_ARCH_KO.md`.

## Current Truth
- The firmware baseline is typed transport v1, not legacy 20-byte live telemetry.
- `src/main.cpp` currently contains the verified behavior but is being split
  toward protocol/lane/control/safety/health modules.
- MCP2515 RX emits `CAN_RX_RAW bus=0`.
- Portenta built-in CAN RX/TX audit uses `bus=1`.
- Host downlink type `10 HOST_CAN_TX_REQUEST` exists for the current bench
  allowlist.
- `CONTROL_ACK` is request-decision evidence; actual CAN TX success is
  `CAN_TX_RAW`.

## Stale Note Handling
If `BOARD_ARCH_KO.md` says current code is still 20-byte only or lacks the basic
host control path, treat that statement as historical context. Use this addendum,
`board/BRIEF.md`, and `shared/docs/TRANSPORT_AND_RECORDS_KO.md` for current
implementation decisions.

