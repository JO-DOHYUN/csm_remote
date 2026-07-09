# CAN_VOLTAGE_BASELINE

## Scope

This is the current implementation baseline after hardware bring-up:
- MCP2515 + TJA1050 bus is `bus=0` and is used for raw CAN receive.
- Portenta built-in CAN on `PH13/PB8` is `bus=1` and is used for raw CAN receive plus actual CAN transmit audit.
- Portenta lab ADC voltage inputs emit `ADC_SAMPLE` records on the same typed stream.
- DBS60E encoder direct A/B/Z capture remains an architecture item, not the first implementation target. Encoder feedback can be interpreted from VCU CAN messages first.

## Verified CAN Baseline

HIL status on 2026-04-22:
- MCP RX via PCAN: PASS, no board drop observed.
- Built-in CAN TX via CAN King monitor: PASS, `CAN_TX_RAW` for ID `0x321`, DLC 8, 500 ms period.
- Voltage raw lane: PASS, `ADC_SAMPLE` source `0`, 12-bit channels `0..3`, `dropped=0`.
- Current firmware environment: `portenta_h7_m7_dual_can_basic`.

Firmware status on 2026-04-23:
- Built-in CAN RX lane added. Frames read by `CAN.available()` / `CAN.read()` are queued as `CAN_RX_RAW` with `bus=1`.

## Voltage Raw Lane

The first voltage lane is a lab evidence lane, not a production isolated field input.

Pins:
- `VoltageSense0`: `A0`, channel id `0`
- `VoltageSense1`: `A1`, channel id `1`
- `VoltageSense2`: `A6`, channel id `2`
- `VoltageSense3`: `A7`, channel id `3`

Do not use `A2/A3/A4/A5` in the MCP bench. On Portenta H7 M7 these aliases overlap the `PC2/PC3` pin family used by MCP SPI `D10/D8`.

Record:
- type `5 ADC_SAMPLE`
- raw ADC counts only, little-endian
- current resolution: 12 bits
- current sample period: 20 ms in `portenta_h7_m7_dual_can_basic`

Scaling is a host/profile responsibility. The raw stream must remain valid even when resistor divider ratios or calibration constants change.

## Implementation Direction

Keep the board as a lane publisher:
- `CanRxLane` owns MCP RX as `CAN_RX_RAW bus=0` and built-in CAN RX as `CAN_RX_RAW bus=1`.
- `CanTxAuditLane` owns successful built-in CAN TX evidence and publishes `CAN_TX_RAW`.
- `VoltageRawLane` publishes direct ADC raw evidence as `ADC_SAMPLE`.
- `HealthMonitor` publishes queue, drop, fault, and capability status.
- `UplinkScheduler` keeps all records in one framed typed stream.

Do not convert voltage samples into fake CAN frames. Do not treat requested TX as actual TX until a successful hardware write is audited by `CAN_TX_RAW`.
