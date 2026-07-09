# HARDWARE_MID_TJA1051_DUAL_CAN_KO

## Purpose
This document fixes the Mid Carrier + TJA1051 production target before large CSM
firmware changes.

Target hardware:
- Arduino Portenta H7 M7
- Portenta Mid Carrier ASX00055
- Adafruit ADA-5708 CAN Pal with TJA1051T/3 for CAN0 physical layer
- Mid Carrier onboard CAN1 physical path exposed on J4 terminal block

MCP2515/TJA1050 is not part of this production target. It remains a
bench/legacy profile only.

## Logical Bus Map
| logical bus | physical path | role | TX policy |
| --- | --- | --- | --- |
| `bus=0` | Mid Carrier J14 `CAN0_TX/RX` + ADA-5708 TJA1051T/3 | monitor/system CAN | disabled until internal CAN0 lane is proven |
| `bus=1` | Mid Carrier J4 terminal CAN1 through onboard U2 | drive/control CAN | allowlist controlled |

The live typed record IDs do not change. `CAN_RX_RAW`, `CAN_TX_RAW`,
`CONTROL_ACK`, `BOARD_EVENT`, `BOARD_HEALTH`, and `CAPABILITY` keep their record
IDs. Bus meaning must be read from `CAPABILITY`.

## Pin and Wiring Checklist
### bus0: ADA-5708 on Mid Carrier J14 CAN0
- J14 pin 26 `CAN0_TX` -> ADA `TX`
- J14 pin 28 `CAN0_RX` <- ADA `RX`
- ADA `Vcc` -> Mid Carrier 3.3 V logic rail
- ADA `GND` -> Mid Carrier logic GND
- ADA terminal `H/L/GND` -> CANH/CANL/reference for the bench bus
- ADA `SLNT` must be normal mode for TX tests; silent mode is RX-only
- ADA termination switch ON only when this node is one physical end of the bus

### bus1: Mid Carrier J4 terminal CAN1
- J4 pin 5 `CANH` -> CANH
- J4 pin 6 `CANL` -> CANL
- J4 GND/reference connected according to bench or field grounding policy
- SW2 CAN bus interface enable/disable state must be recorded before HIL claims
- Termination must be measured with power off; 1:1 bench expects about 60 ohm
  across CANH/CANL when both endpoints are terminated

## Firmware Constraints
- ADA-5708/TJA1051T/3 is a transceiver only. Do not add an ADA/TJA1051 library
  as if it were a CAN controller.
- The current ArduinoCore-mbed Portenta H7 variant exposes one global `CAN`
  object on `PH13/PB8`, labeled CAN1 on the high-density connector.
- CAN0 requires a repo-local STM32 internal CAN backend or a verified core variant
  update before firmware may advertise `rx_supported=1` or `tx_supported=1` for
  `bus=0`.
- Until CAN0 HAL is implemented, the Mid profile may describe the physical bus0
  target in `CAPABILITY` but must mark the backend as pending/unavailable.

## CAPABILITY Requirements
Profile major `2` means Mid Carrier target. It must include per-bus descriptors:
- bus id
- role
- physical backend
- transceiver type
- RX/TX support flags
- control TX permission
- classic CAN support
- Classic CAN 2.0 capability flag; CAN FD is out of scope
- max live DLC
- nominal bitrate
- termination policy
- isolation policy

Qt/VMS must not label `bus=0` as MCP when profile major `2` is received.

## HIL Order
1. Power-off continuity and termination measurement for bus0 and bus1.
2. Boot typed stream and verify CAPABILITY profile major `2`.
3. Verify `BOARD_HEALTH` is periodic and does not report MCP fields as
   production health.
4. Verify bus1 J4 RX with PCAN or CAN analyzer.
5. Verify bus1 host TX request: `CONTROL_ACK` then `CAN_TX_RAW`.
6. Implement and compile internal CAN0 Classic CAN 2.0 bus0 backend.
7. Verify bus0 ADA RX smoke.
8. Verify bus0 ADA TX only after silent/standby and termination are confirmed.
9. Run simultaneous bus0/bus1 RX load and check visible drop counters/events.

Build success is not HIL evidence. Upload, typed stream decode, and CAN analyzer
evidence must be reported separately.
