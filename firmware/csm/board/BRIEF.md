# BRIEF.md - Board

## Current Baseline
- baseline source: `src/main.cpp`, `platformio.ini`
- root routing source: `AGENTS.md`, `BRIEF.md`
- encoder data sheet: `dataSheet_DBS60E-THEJD2048_1116617_ko.pdf`
- final hardware concept: `board/docs/HARDWARE_FINAL_CONCEPT_KO.md`
- current CAN + voltage baseline: `board/docs/CAN_VOLTAGE_BASELINE.md`
- current Passive Product CSM CAN: Portenta H7 M7 + Mid Carrier ASX00055 +
  MCP2515/TJA1050 SPI module and Mid Carrier J4/U2 built-in CAN RX. `bus=0`
  is MCP2515/TJA1050 at `MCP_8MHZ`, `CAN_500KBPS`, 8 MHz SPI, polling drain
  (`BOARD_CAN_IRQ_MODE=0`), `BOARD_CAN_SERIAL_DRAIN_BUDGET=512`,
  `CS/SCK/SI/SO/INT` level shifted. In the passive field build, both bus0 and
  bus1 CAN front ends are uninitialized during USB power-up, then initialized
  only after stable CDC/DTR session plus quiet window before ACK-observe. Host
  downlink, control TX, CAN TX tests, and USB disconnect reset are compile-time
  forbidden for the passive artifact.
- Full Instrumented keeps the active-capable dual CSM path for bench/HIL only:
  MCP2515/TJA1050 `bus=0` plus Mid Carrier J4/U2 `bus=1`, host control, safety
  gate, and audited `CAN_TX_RAW`.
- deferred hardware target: dual internal CAN0/CAN1 through TJA1051-class
  transceivers. This is not active because the current ArduinoCore Portenta
  profile exposes only one practical CAN object.
- board target: Arduino Portenta H7 M7, `framework = arduino`
- current firmware protocol: framed typed records v1
  - SOF `0xA5 0x5A`, version, record type, flags, sequence, payload length, payload, CRC16-CCITT
  - legacy fixed 20B serial telemetry is no longer the default firmware output
- target contract: `shared/docs/TRANSPORT_AND_RECORDS_KO.md` typed records with board-local `mono64`
- verified build status: PASS, 2026-04-22, `portenta_h7_m7_mcp_int_main`, `portenta_h7_m7_dual_can_basic`
- verified capture path: PASS, 2026-04-22, MCP2515+TJA1050 5 V module at `CAN_500KBPS` received PCAN ID `0x202`, DLC 8, 10 ms period, `can_ok=1`, `can_drop=0`, `queue=0`, `fault=0x00000000`
- verified INT status: PASS, 2026-04-22, `portenta_h7_m7_mcp_int_main` uses MCP2515 `INT_N` as an active-low level hint with bounded polling fallback. HIL log showed `flags=0x0A` (`can_ok=1`, MCP INT level mode enabled), PCAN 10 ms RX, `can_drop=0`.
- verified dual baseline flash: PASS, 2026-04-22, `portenta_h7_m7_dual_can_basic` uploaded by DFU and streamed on `COM6`. MCP RX stayed healthy with PCAN on MCP (`can_rx` increasing, `can_drop=0`, `fault=0x00000000`). Built-in CAN controller initialized (`builtin_can_tx_ok=1`), but write attempts reported `EventBuiltinCanTxFailed` while PCAN was still connected only to MCP; this does not validate built-in CAN physical TX until a separate transceiver/bus node ACKs it.
- verified CAN + voltage stream after voltage-lane edit: PASS, 2026-04-22, `portenta_h7_m7_dual_can_basic` uploaded by DFU. Stream contained `CAN_RX_RAW` on `bus=0` ID `0x530` with `can_drop=0`, `CAN_TX_RAW` on `bus=1` ID `0x321` with TX success counter increasing, and `ADC_SAMPLE` source `0` channels `0..3` at 12-bit raw counts. Health showed `flags=0x2E` (`can_ok=1`, `builtin_can_tx_ok=1`, `mcp_int_level=1`, `voltage_adc_ok=1`) and `fault=0x00000000`.
- verified built-in CAN RX lane: PASS, 2026-04-23, `portenta_h7_m7_dual_can_basic` uploaded on `COM6`. Stream contained `CAN_RX_RAW bus=1` from the Portenta built-in CAN bus, `CAN_TX_RAW bus=1 id=0x321`, `CAN_RX_RAW bus=0 id=0x530`, `ADC_SAMPLE`, and `BOARD_HEALTH` with `can_drop=0`, `queue=0`, `fault=0x00000000`.
- verified host control downlink: PASS, 2026-04-23, `HOST_CAN_TX_REQUEST` from USB was parsed and written to built-in CAN. Tests sent `0x503` and `0x510`; board returned `CONTROL_ACK status=1 reason=0` followed by `CAN_TX_RAW bus=1` for each, with `failed=0`.
- verified Mid Carrier MCP RX/TX bring-up: PASS, 2026-05-15,
  `portenta_h7_m7_mcp_int_tx_test` uploaded by DFU and streamed on `COM7`.
  MCP2515/TJA1050 at `MCP_8MHZ`, `CAN_500KBPS` received PCAN traffic as
  `CAN_RX_RAW bus=0`, health showed `can_ok=1`, and board TX test emitted
  `CAN_TX_RAW bus=0 id=0x321 failed=0`; PCANBasic read five `0x321` frames.
- verified Mid Carrier CSM TX/RX audit: PASS, 2026-05-15,
  `portenta_h7_m7_mid_mcp2515_csm_tx_test` uploaded on `COM7` with 2 MHz SPI.
  Stream showed `CAN_RX_RAW bus=0`, periodic `CAN_TX_RAW bus=0 id=0x321
  failed=0`, `BOARD_HEALTH can_ok=1 can_drop=0 fifo_overflow=0`, and repeated
  `CAPABILITY profile=3 bus0 role=drive/control backend=MCP2515`.
  Host request `0x503` returned `CONTROL_ACK status=1` followed by matching
  `CAN_TX_RAW failed=0`.
- verified Mid Carrier dual CSM final env: PASS, 2026-05-15,
  `portenta_h7_m7_mid_mcp2515_j4_dual_csm` uploaded on `COM7`. `CAPABILITY`
  advertised `bus0: MCP2515/TJA1050 monitor/system` and
  `bus1: ArduinoCAN/MidCarrierU2 drive/control`. Kvaser on J4 sent/received at
  500 kbps with zero error frames after the J4 SW2 pair was moved to the
  CAN-enabled position. Host `bus=1 id=0x503` produced
  `CONTROL_ACK status=1`, `CAN_TX_RAW bus=1 failed=0`, and Kvaser received the
  same payload. Host `bus=0 id=0x503` produced `CONTROL_ACK status=1` and
  `CAN_TX_RAW bus=0 failed=0`.
- current rebuild direction, 2026-06-29: split outputs into Passive Product and
  Full Instrumented. Passive Product is the default build and cannot be accepted
  as `verified_passive` until hardware safety case and bench verification IDs
  are nonzero. Full Instrumented remains active-capable and must not be used as
  passive acceptance evidence.
- known failed INT design: the previous edge-gated `BOARD_CAN_USE_INT=1` implementation caused red blinking. Do not restore that design. MCP2515 `INT_N` is level-signaled, so RX authority must come from MCP2515 status/register drain, not from a single falling-edge gate.

## Current Architecture Goal
- preserve typed capture truth while splitting vehicle passive evidence from
  active bench/HIL control
- keep the current Mid Carrier MCP2515 CSM profile explicit in CAPABILITY,
  health, and build profiles before revisiting a different controller path
- treat `shared/docs/TRANSPORT_AND_RECORDS_KO.md` as the canonical wire contract
- treat `VMS_CSM_03_ARCHITECT_SYNTHESIS_FINAL.md` as the latest integration
  decision input, not as a replacement for scoped AGENTS/BRIEF docs
- do not claim physical flash/HIL success without an actual hardware run
- do not change record schema beyond the shared typed-record contract without
  updating that shared contract in the same turn

## Final Design Decision
- keep the old 20B stream as a documented legacy compatibility format only
- next production direction is a lane-separated evidence board:
  - `TypedFrame` / `TypedRecords`
  - `CanRxLane`
  - `CanTxAuditLane`
  - `EncoderEdgeLane`
  - `AnalogSampleLane`
  - `ControlLane`
  - `SafetySupervisor`
  - `UplinkScheduler`
  - `HealthMonitor`
- all lanes publish typed records on one monotonic board time axis
- direct board sensor samples must not be encoded as fake CAN frames
- host control must be auditable from intent to board decision to actual CAN TX
  to feedback/event in Full Instrumented only; `CONTROL_ACK` is not final CAN
  success evidence
- drive encoder input is fixed as industrial HTL front-end plus Portenta `PC6`/`PC7` TIM3 encoder mode and `PA8` index input; direct 24 V HTL to GPIO is forbidden
- carrier board owns field protection, isolation, power monitoring, CAN physical layer, external ADC front-end, and hard safety gate
- Passive Product uses MCP2515 over SPI on `D7..D11` for `bus=0` and Mid Carrier
  J4/U2 for `bus=1`; both lanes are deferred through USB power-up and enter
  ACK-observe only after stable host session plus quiet window. It emits
  `CAN_RX_SEGMENT` for both buses and does not accept host CAN TX requests.
- Passive Product host-absent mode is safe-receive-and-discard. It must not stage
  typed CAN payload for later replay, and session-open quarantine clears
  CDC/uplink/session state before enabling ACK-observe.
- Capability v6 hardware evidence fields are claims/references. They support
  mismatch and operator diagnostics but are not verified proof without external
  analyzer/scope/DTC artifacts.
- The product is two-bus only. Missing-bus diagnostics remain required to catch
  wrong firmware upload or wiring/profile mismatch.
- Full Instrumented additionally exposes the J4 CAN1 terminal as `bus=1`
  through the onboard U2 transceiver and accepts allowlisted host TX requests
  through the safety gate.
- firmware bus descriptors expose physical backend and capability; role is only
  a non-authoritative hint.
- the previous dual internal CAN direction remains a deferred research path, not
  the active CSM hardware contract.

## Working Features To Preserve
- current CAN raw capture remains truthful
- current serial upload remains recoverable/resyncable through SOF + length + CRC16
- current basic stats visibility remains available
- existing `dropped_total` and packet sequence visibility remain present until replaced by typed `BOARD_HEALTH` / `BOARD_EVENT`

## Acceptance
- `mono64` migration is explicitly specified before replacing the current `uint32_t micros()` field
- raw CAN record migration is documented in `shared/docs/TRANSPORT_AND_RECORDS_KO.md`
- drop/overflow visibility is preserved or improved
- control and capture paths cannot hide side effects from each other
- compile succeeds for the current firmware baseline after document finalization
- `portenta_h7_m7_mid_mcp2515_csm` compiles and HIL verifies MCP RX/TX before
  larger code changes begin

## Risks
- open items:
  - Arduino_CAN does not currently expose hardware FIFO overflow or bus-state counters in this implementation
  - component-level schematic symbols, PCB layout, and exact production BOM are not generated in this repository yet
  - QT parser/storage work must be synchronized with framed typed records v1
- manual checks:
  - CAN flood and hidden-drop check
  - encoder pulse injection check after `EncoderEdgeLane` exists
  - ADC burst starvation check after `AnalogSampleLane` exists
  - lease timeout, reconnect, and estop transition check after `ControlLane` exists
