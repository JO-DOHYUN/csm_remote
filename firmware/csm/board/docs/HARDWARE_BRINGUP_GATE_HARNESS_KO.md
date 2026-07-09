# HARDWARE_BRINGUP_GATE_HARNESS_KO

## Purpose
This is the hardware bring-up harness for agents. Its job is to finish embedded
hardware setup with the fewest repeated tests, tokens, and user measurements.
It is stricter than a checklist: the agent must not move to the next layer until
the current gate has evidence.

Use this document only for physical hardware setup, wiring, power, level
shifting, controller access, CAN bus bring-up, upload recovery, and HIL smoke
verification.

## Agent Contract
Before any hardware test, the agent must write one short declaration:

```text
Test intent: confirm or exclude <one hypothesis>.
Pass evidence: <exact voltage, log, register, typed record, or analyzer frame>.
Fail branch: <next single test if this fails>.
```

The agent must keep a local ledger during the bring-up:

```text
Confirmed facts:
- ...

Unconfirmed hypotheses:
- ...

Already tested:
- test, condition, result, timestamp or log cue

Next single test:
- ...
```

If a test would repeat an already tested objective, the agent must stop and
summarize the ledger before asking for any new measurement.

## Non-Repetition Rules
- Do not repeat a test with the same wiring, firmware, PCAN state, and
  measurement point.
- A repeat is allowed only if at least one variable changed:
  - wiring changed
  - firmware/env changed
  - power source or rail changed
  - PCAN/termination/bitrate changed
  - measurement point changed
  - previous reading was missing or ambiguous
- If the user reports a physical measurement, treat it as a confirmed fact unless
  a later measurement or log contradicts it.
- Never ask the user to re-check a confirmed pin, rail, or continuity point just
  because the next layer failed.

## Gate 0: Target Freeze
Goal: prevent wrong architecture work.

Must record:
- exact board stack
- controller vs transceiver distinction
- active PlatformIO env
- expected CAN bitrate
- expected controller clock
- expected logical bus ids

Pass evidence:
- the agent can state the controller and transceiver separately.

Fail branch:
- stop code/upload work and resolve hardware target first.

## Gate 1: Power and Ground
Goal: confirm the module can electrically operate.

Must record:
- module VCC at the module pin
- Portenta logic voltage reference
- level shifter low-side and high-side rails
- common GND
- OE or enable pin state if the shifter has one

Pass evidence:
- expected rails are present under connected load.

Fail branch:
- fix power, OE, or GND before any SPI/CAN test.

## Gate 2: Direction and Level Shifting
Goal: prevent reverse-direction or 5 V input damage mistakes.

For MCP2515:
- `CS/SCK/SI`: MCU -> MCP
- `SO/INT`: MCP -> MCU
- `SO/INT` from a 5 V MCP module must be shifted down before Portenta input.
- `INT` is optional for polling, but `CS` is mandatory.

Pass evidence:
- a direction table exists before probing.

Fail branch:
- do not continue to pin truth or SPI until direction is fixed.

## Gate 3: MCU Pin Truth
Goal: prove the MCU pin map once without external load.

Allowed tests:
- free-wire output toggle for CS/SCK/SI
- input pull-up/down truth for SO/INT
- SPI loopback for COPI/CIPO if needed

Pass evidence:
- pin toggles or reads match the repo pin map.

Fail branch:
- inspect `BoardPins.h`, PlatformIO env, and Portenta variant mapping.

Stop rule:
- after this gate passes, do not re-question pin mapping unless a later direct
  contradiction appears.

## Gate 4: Connected-Node Electrical Check
Goal: identify load, shifter direction, OE, or short problems.

Must compare:
- free-wire voltage behavior
- connected-node voltage behavior
- module-side voltage
- MCU-side voltage

Pass evidence:
- each line has the expected idle and driven levels on the correct side.

Fail branch:
- if a line collapses only when connected, inspect shifter direction, OE, module
  load, and module pin identity. Do not move to SPI register tests yet.

## Gate 5: Controller Register Access
Goal: prove the controller is reachable before any CAN bus reasoning.

For MCP2515:
- read `CANSTAT`, `CANCTRL`, `CANINTF`, `EFLG`.
- fixed `0xFF` means open/floating MISO or wrong access path.
- fixed `0x00` means held-low, power, CS, shifter, or bus contention class.

Pass evidence:
- registers change plausibly across reset/config/normal mode.

Fail branch:
- stay in SPI/electrical diagnosis. Do not inspect PCAN, CANH/CANL, or
  termination yet.

## Gate 6: Controller Init
Goal: prove firmware can configure the controller.

For MCP2515:
- reset succeeds.
- `setBitrate(CAN_500KBPS, MCP_8MHZ)` succeeds.
- normal mode succeeds.
- status snapshot is logged.

Pass evidence:
- init returns OK and health can later report `can_ok=1`.

Fail branch:
- inspect controller clock, SPI speed, CS timing, and library env. Do not claim
  CAN bus failure yet.

## Gate 7: Physical CAN Bus
Goal: prove the controller has an ACK-capable bus.

Must record:
- PCAN channel
- bitrate
- termination state
- CANH/CANL orientation
- whether another ACK-capable node is present

Pass evidence:
- PCAN reads bus traffic or receives the board test frame.

Fail branch:
- inspect CANH/CANL, termination, transceiver power, and ACK presence.

## Gate 8: Typed Firmware Evidence
Goal: prove firmware publishes real board evidence.

Pass evidence:
- `BOARD_HEALTH can_ok=1`
- `CAN_RX_RAW bus=N` for incoming frames
- `CAN_TX_RAW bus=N failed=0` for successful actual writes
- visible `BOARD_EVENT` for faults or drops

Fail branch:
- if controller and CAN bus passed, inspect typed transport and firmware lane
  plumbing. Do not go back to pin map without contradiction.

## Gate 9: Control Audit
Goal: prove the CSM is a control gateway, not just a smoke transmitter.

Pass evidence:
- host sends `HOST_CAN_TX_REQUEST`
- board emits `CONTROL_ACK status=1`
- board emits matching `CAN_TX_RAW failed=0`
- PCAN sees the same CAN id and data

Fail branch:
- if `CONTROL_ACK` rejects, inspect policy.
- if `CONTROL_ACK` accepts but no `CAN_TX_RAW`, inspect TX lane.
- if `CAN_TX_RAW` appears but PCAN does not see it, inspect physical CAN.

## MCP2515 Fast Path
For the current Mid Carrier MCP2515 CSM setup, use this order:

1. Confirm MCP module 5 V, common GND, and level-shifter rails.
2. Confirm direction: `CS/SCK/SI` up-shift, `SO/INT` down-shift.
3. Confirm free-wire pin truth once.
4. Confirm connected-node voltage only if free-wire truth passed.
5. Confirm MCP raw registers.
6. Confirm MCP init at `MCP_8MHZ`, `CAN_500KBPS`.
7. Confirm PCAN at 500 kbps with termination.
8. Confirm `CAN_RX_RAW bus=0`.
9. Confirm host-command TX audit on `bus=0`.

## Required Final Report
A hardware bring-up report must include:
- gates passed and gates skipped
- failed or repeated test count
- confirmed root cause or remaining unknown
- exact final firmware env
- exact evidence logs for RX, TX audit, and analyzer confirmation
- remaining risk, especially isolation, protection, overflow, and soak status
