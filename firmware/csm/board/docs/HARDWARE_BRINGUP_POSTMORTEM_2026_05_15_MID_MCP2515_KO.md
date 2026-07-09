# HARDWARE_BRINGUP_POSTMORTEM_2026_05_15_MID_MCP2515_KO

## Purpose
This postmortem records the Mid Carrier + MCP2515/TJA1050 bring-up failure path
from setup start to working CSM evidence. It is not a blame note. Its purpose is
to prevent repeated tests, reduce token/time cost, and show the fastest route
that should have been taken.

## Final Working State
- Board stack: Portenta H7 M7 + Portenta Mid Carrier ASX00055.
- CAN controller/transceiver: external MCP2515 + TJA1050 module.
- Bus: Classic CAN 2.0, 500 kbps.
- MCP clock: `MCP_8MHZ`.
- Firmware env: `portenta_h7_m7_mid_mcp2515_csm`.
- Active logical bus: `bus=0`.
- Evidence confirmed:
  - MCP RX appears as `CAN_RX_RAW bus=0`.
  - Host command on `bus=0 id=0x503` returns `CONTROL_ACK status=1 reason=0`.
  - Actual TX audit appears as `CAN_TX_RAW bus=0 failed=0`.
  - PCANBasic receives the same board TX frame.
  - Final dual CSM env also exposes Mid Carrier J4 CAN1 as `bus=1` through U2.
  - Kvaser on J4 receives host-command board TX `bus=1 id=0x503` and the board
    receives J4 traffic as `CAN_RX_RAW bus=1`.

## Confirmed Final Wiring Rules
| Signal | Direction | Mid Carrier pin | MCP2515 module side | Rule |
| --- | --- | --- | --- | --- |
| CS | MCU -> MCP | D7 / SPI CS | CS | level shift 3.3 V to 5 V |
| SI | MCU -> MCP | D8 / COPI | SI | level shift 3.3 V to 5 V |
| SCK | MCU -> MCP | D9 / SCK | SCK | level shift 3.3 V to 5 V |
| SO | MCP -> MCU | D10 / CIPO | SO | level shift 5 V to 3.3 V |
| INT | MCP -> MCU | D11 | INT | level shift 5 V to 3.3 V |
| VCC | power | 5 V rail | VCC | module powered as 5 V module |
| GND | reference | logic GND | GND | common ground required |
| CANH/CANL | CAN bus | PCAN bus | H/L | termination and 500 kbps required |

J4 CAN1 note: the Mid Carrier J4 path depends on the SW2 pair. The initial
position produced only Kvaser error frames and board-side `CAN.write()` failure.
Moving both SW2 poles together to the opposite position enabled J4 CAN1; after
that, Kvaser reported zero error frames and `CAN_RX_RAW bus=1` appeared.

## Failure and Repetition History
Counts below are grouped by objective. Repeated multimeter checks with the same
purpose are counted as one failure group unless the condition changed.

| Count | Failure group | What happened | Correct interpretation |
| ---: | --- | --- | --- |
| 1 | Wrong hardware target path | The initial Mid Carrier + TJA1051 dual-internal-CAN direction was treated as production-ready. | TJA1051 is only a transceiver, and the current ArduinoCore Portenta profile exposes only one practical CAN object. The working route was MCP2515. |
| 1 | Directional level shifter not locked early | `SO` and `INT` direction were not separated from `CS/SI/SCK` early enough. | Direction must be fixed before voltage or SPI tests: MCU-to-MCP and MCP-to-MCU are different directions. |
| 4 groups | Pin and voltage checks repeated | D7/D8/D9 output, CS/SI/SCK/SO/INT voltage, high latch, and SO detach checks were revisited after similar facts were already known. | Once free-wire pin truth passes, later failures should move to connected-node load, shifter direction, or module-side conditions. |
| 1 | SPI and CAN layers mixed | MCP register uncertainty was considered near PCAN/termination/CAN receive questions. | Raw register failure is before CAN. Do not inspect CAN bus until SPI register access is proven. |
| 1 | PCAN state not locked before TX judgment | Board TX failed while PCAN or ACK condition was not fully locked. | A CAN TX failure without an ACK-capable node is not firmware proof. Confirm PCAN connected, bitrate, and termination first. |
| 1 | Upload recovery path delayed | Runtime USB disappeared after a diagnostic firmware upload. | If COM reset fails but DFU is visible, use direct DFU upload immediately. |
| 1 | CSM completion gap | RX/TX smoke worked before host-command TX on MCP `bus=0` was implemented. | CSM is complete only after host request, `CONTROL_ACK`, `CAN_TX_RAW`, and PCAN receive all pass. |
| 1 | J4 SW2 state missed | Mid Carrier J4 showed only Kvaser error frames while the SW2 pair was in the initial position. | Treat J4 SW2 as a physical gate. If Kvaser sees error frames only and board `bus=1` TX fails, flip both SW2 poles together before changing firmware. |

## Fast Path That Should Have Been Used
1. Freeze target: Mid Carrier + external MCP2515/TJA1050, not TJA1051 dual internal CAN.
2. Freeze level-shifter direction before any voltage interpretation:
   `CS/SCK/SI = MCU -> MCP`, `SO/INT = MCP -> MCU`.
3. Confirm power once: MCP module 5 V, common GND, Portenta logic 3.3 V.
4. Run one free-wire pin truth test for D7..D11. After pass, do not re-question pin mapping without new contradictory evidence.
5. Run connected-node voltage test. If a line collapses only when connected, inspect shifter direction, OE, load, or module-side short.
6. Run MCP raw register test. If `0xFF` or `0x00` is fixed, stay in SPI/electrical diagnosis.
7. Run MCP init: reset, `setBitrate(CAN_500KBPS, MCP_8MHZ)`, normal mode.
8. Confirm PCAN: 500 kbps, termination, connected bus, ACK-capable node.
9. Confirm typed evidence: `CAN_RX_RAW bus=0`, `BOARD_HEALTH can_ok=1`.
10. Confirm control gateway: host request -> `CONTROL_ACK` -> `CAN_TX_RAW bus=0 failed=0` -> PCAN receives same frame.

## Concrete Lessons
- Do not re-test a pin map after free-wire pin truth passes unless a new measurement contradicts it.
- Do not use PCAN, termination, or CAN RX as evidence while MCP register access is still failing.
- Do not collapse `SO` and `INT` into the same direction group as `CS/SI/SCK`.
- Do not call a board TX failure meaningful unless an ACK-capable CAN node is present.
- Do not treat a smoke TX test as CSM completion. CSM completion requires host-command audit.

## Current Residual Risk
- `BOARD_EVENT code=9` can appear under live traffic, indicating MCP error or overflow evidence. It does not invalidate the confirmed RX/TX path, but flood/soak tuning remains separate HIL work.
- The current setup is a non-isolated bench/current harness. Final field hardware still needs isolation, protection, and safety gate validation.
