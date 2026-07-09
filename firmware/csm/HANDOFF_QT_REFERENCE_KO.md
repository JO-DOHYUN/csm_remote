# HANDOFF_QT_REFERENCE_KO

## Purpose

This package is a reference handoff from the Portenta board bring-up project to the Qt project. The Qt project cannot see the original chat, so this file summarizes the verified state, active contracts, and next implementation direction.

Date: 2026-04-23  
Project: `J_ArdP7_AM2_CSM`  
Board target: Arduino Portenta H7 M7, PlatformIO `ststm32`, Arduino framework

## Current Verified Board State

The board is no longer just a CAN-USB test sketch. It is now a typed-record evidence board baseline.

Verified on hardware:
- MCP2515 + TJA1050 CAN bus receive works.
- Portenta built-in CAN transmit works through external CAN transceiver.
- Portenta built-in CAN receive lane is implemented in firmware and emits `CAN_RX_RAW bus=1`.
- PCAN monitors/sends on MCP bus.
- CAN King monitors built-in CAN bus on a second notebook.
- USB typed binary stream works on `COM6`.
- Voltage raw ADC lane works and is included in the same stream.

Latest flashed environment:
- `portenta_h7_m7_dual_can_basic`

Important HIL observations:
- `CAN_RX_RAW bus=0 id=0x530` received from MCP side.
- `CAN_TX_RAW bus=1 id=0x321` emitted when built-in CAN TX succeeds.
- `CAN_RX_RAW bus=1` is the expected record when a separate node transmits on the built-in CAN bus.
- `ADC_SAMPLE source=0 bits=12 ch0..ch3` emitted at 20 ms period.
- Health showed `flags=0x2E`:
  - `can_ok=1`
  - `builtin_can_tx_ok=1`
  - `mcp_int_level=1`
  - `voltage_adc_ok=1`
- `can_drop=0`
- `fault=0x00000000`

## Hardware Channel Map

CAN channels:
- `bus=0`: MCP2515 + TJA1050 module over SPI, used as raw receive lane.
- `bus=1`: Portenta built-in CAN on `PH13/PB8`, used for built-in CAN RX records and actual TX audit records.

MCP2515 wiring:
- `D7` -> MCP `CS`
- `D9` -> MCP `SCK`
- `D8` -> MCP `SI/MOSI`
- `D10` <- MCP `SO/MISO`
- `D11` <- MCP `INT_N`
- MCP module is powered at 5 V and uses level shifting for SPI/INT.
- MCP crystal is 8 MHz.
- CAN speed is 500 kbps.

Built-in CAN:
- `PH13` -> external transceiver `TXD`
- `PB8` <- external transceiver `RXD`
- Use a separate physical CAN bus from MCP.
- CAN speed is 500 kbps.

Voltage raw inputs:
- `ch0 = A0`
- `ch1 = A1`
- `ch2 = A6`
- `ch3 = A7`
- Resolution: 12-bit raw ADC count.
- Sample period in current env: 20 ms.

Do not use `A2/A3/A4/A5` for the current MCP bench. They overlap the `PC2/PC3` SPI pin family used by MCP SPI on this Portenta mapping.

Never feed vehicle voltage directly into Portenta ADC. Use divider/protection/isolation so MCU pin voltage stays within `0..3.3 V`.

## Active Binary Contract

The active contract is in:
- `shared/docs/TRANSPORT_AND_RECORDS_KO.md`

Transport frame:
- SOF `0xA5 0x5A`
- `version u8`
- `record_type u8`
- `flags u8`
- `seq u16_le`
- `payload_len u16_le`
- payload
- `crc16_ccitt_le`

Record types currently important for Qt:
- `1 CAN_RX_RAW`
- `2 CAN_TX_RAW`
- `5 ADC_SAMPLE`
- `7 BOARD_EVENT`
- `8 BOARD_HEALTH`
- `9 CAPABILITY`
- `10 HOST_CAN_TX_REQUEST` host-to-board downlink

Encoder direct records are defined but not the immediate implementation target. For now, wheel/encoder feedback should be interpreted from VCU CAN messages.

## Qt Target Architecture

Qt should treat the board as a raw evidence source.

Required runtime split:
- `TransportRuntime`: USB serial, frame resync, CRC validation, typed dispatch.
- `StorageRuntime`: append exact original framed bytes to `capture.stream.part`.
- `AnalysisRuntime`: CAN DB decode, VCU encoder feedback interpretation, voltage scaling.
- `EvidenceRuntime`: expose source identity, drop/fault counters, mismatch states.
- `ControlRuntime`: later Qt-to-board wheel command path.
- `OperatorRuntime` / UI: operator-visible state, not raw truth mutation.

Storage rule:
- The original typed stream is source of truth.
- CAN decode results, voltage scaling, graph cache, and operator notes are sidecars.
- Do not store voltage samples as fake CAN frames.
- Do not treat a requested CAN TX as successful until the board emits `CAN_TX_RAW`.

Qt storage design document:
- `qt/docs/QT_TYPED_RECORD_STORAGE.md`

## Current Board Code Architecture

The current code is still in a single `src/main.cpp`, but the logical lanes are now separated:
- `CanRxLane`: MCP receive -> `CAN_RX_RAW bus=0`; built-in CAN receive -> `CAN_RX_RAW bus=1`
- `ControlLane`: parses Qt `HOST_CAN_TX_REQUEST`, validates bus/ID/frame shape, emits `CONTROL_ACK`, and writes accepted frames on built-in CAN
- `CanTxAuditLane`: built-in CAN successful TX -> `CAN_TX_RAW`
- `VoltageRawLane`: `A0/A1/A6/A7` raw ADC -> `ADC_SAMPLE`
- `HealthMonitor`: `BOARD_HEALTH`, `BOARD_EVENT`
- `UplinkScheduler`: framed typed stream over USB serial

Current host TX downlink:
- Qt sends typed frame record type `10`.
- Payload is 19 bytes: `command_id u32`, `bus u8`, `frame_flags u8`, `can_id u32`, `dlc u8`, `data[8]`.
- Board accepts only `bus=1` standard CAN IDs `0x503`, `0x510`, `0x511`, `0x512`, `0x513`.
- Accepted write evidence is `CONTROL_ACK status=1 reason=0` followed by `CAN_TX_RAW bus=1`.

Important implementation files:
- `src/main.cpp`
- `include/BoardPins.h`
- `platformio.ini`
- `pc_tools/verify_typed_stream.py`
- `pc_tools/log_capture.py`

## What Is Not Finished

The capture/audit baseline is ready. The basic typed host CAN TX request path is
implemented on the board for the current bench allowlist. The production control
platform is not finished yet.

Still needed:
- Safety lease/heartbeat.
- Arm/disarm state.
- Neutral/timeout behavior.
- Production TX queue and rate limiting.
- Capability-driven control enable/disable policy.
- Actual wheel command CAN frame mapping.
- CAN DB/profile selection in Qt.
- Voltage calibration profile in Qt.

The next stage is not “just send wheel CAN from Qt.” It must be:
1. Qt command message
2. Board validates command and current safety state
3. Board sends actual CAN frame
4. Board emits `CAN_TX_RAW` only on successful hardware write
5. Qt shows request, accept/reject, actual TX, and fault state separately

## Useful Commands

Build current board baseline:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_dual_can_basic
```

DFU upload:

```powershell
& "$env:USERPROFILE\.platformio\packages\tool-dfuutil-arduino\dfu-util.exe" -d 2341:035b -a 0 -s 0x08040000:leave -D .pio\build\portenta_h7_m7_dual_can_basic\firmware.bin
```

Decode live stream:

```powershell
python pc_tools\verify_typed_stream.py --port COM6 --seconds 10 --raw
```

Capture raw stream for Qt replay work:

```powershell
python pc_tools\log_capture.py --port COM6 --seconds 60 --out capture.stream
```

## Recommended Qt First Implementation

1. Implement typed frame parser with CRC and resync.
2. Append exact frames to `capture.stream.part`.
3. Decode and display `CAPABILITY`, `BOARD_HEALTH`, `BOARD_EVENT`.
4. Decode `CAN_RX_RAW` and `CAN_TX_RAW`, preserving `bus`.
5. Decode `ADC_SAMPLE`, preserving raw counts and channel IDs.
6. Add CAN DB decode sidecar for VCU/wheel/encoder feedback.
7. Add voltage calibration sidecar.
8. Only after capture/replay parity is proven, add wheel control command path.
