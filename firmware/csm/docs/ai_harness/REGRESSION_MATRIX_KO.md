# REGRESSION_MATRIX_KO

## CSM Regression - passive_product
- default env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`
- passive guard reports zero linked active symbols for host downlink/control
  TX/CAN TX tests/USB forced reset. MCP normal mode is allowed only through the
  ACK-observe session state machine.
- `CAPABILITY fw_profile=1`, `host_cmd_rx=0`, `control_path=0`,
  `dtr_reset_sensitive=0`, `dtr_session_required=1`,
  `dtr_session_only=1`, `passive_acceptance=0` unless hardware evidence IDs are
  explicitly configured
- MCP2515 starts pre-session safe, advertises ACK-observe capability, and enters
  normal observe only after session quarantine
- Mid Carrier J4/U2 bus1 starts monitor/safe, advertises ACK-observe capability,
  and enters observe only after session quarantine
- `BOARD_HEALTH v6 passive_violation=0`, `serial_clear=0`,
  `serial_enqueue_fail=0`, `uplink_pool_alloc_fail=0` during smoke after the
  USB CDC host session opens
- Before the USB CDC host session opens, CSM must not stage capability/health/
  event records into the uplink payload pool; session-open must re-emit
  capability and health evidence

## CSM Regression - common
- typed frame SOF/length/CRC resync
- `CAPABILITY` record after boot
- `BOARD_HEALTH` periodic record
- host type 10 accepted ids -> `CONTROL_ACK` then `CAN_TX_RAW`
- rejected host commands -> `CONTROL_ACK status=0` and visible reason
- ADC raw -> `ADC_SAMPLE`, not fake CAN

## CSM Regression - legacy_mcp
- MCP RX -> `CAN_RX_RAW bus=0`
- built-in CAN RX -> `CAN_RX_RAW bus=1`
- MCP INT level hint + bounded polling, no falling-edge-only gate

## CSM Regression - mid_mcp2515_csm_legacy_mcp_only
- legacy/bench-only MCP regression; not a Passive Product artifact and not a
  current vehicle acceptance target
- env `portenta_h7_m7_mid_mcp2515_csm` exposes only the MCP descriptor for
  `bus=0`
- MCP RX -> `CAN_RX_RAW bus=0`
- host type 10 on `bus=0` accepted IDs -> `CONTROL_ACK` then `CAN_TX_RAW bus=0`
- PCAN sees the audited board TX frame
- MCP INT level hint + bounded polling, no falling-edge-only gate

## CSM Regression - mid_mcp2515_j4_dual_csm_full_instrumented
- bench/HIL env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm_full_instrumented`
- `CAPABILITY` profile major 3 exposes `bus=0` MCP2515/TJA1050 and `bus=1`
  Mid Carrier J4/U2 descriptors
- bus descriptor role is a non-authoritative `role_hint`; VMS must not hard-code
  System/Drive from bus id or descriptor role
- live RX uplink uses `CAN_RX_SEGMENT`; every segment entry is one factual CAN
  frame and must preserve `capture_seq64`, `mono_us`, bus, id, DLC, and payload
- `BOARD_HEALTH` shows `can_drop=0`, `fifo_overflow=0`, `queue=0`, `fault=0`
  during final smoke
- production host TX before heartbeat+arm is rejected with `CONTROL_ACK reason=9`
  or `10`
- `HOST_HEARTBEAT` then `HOST_CONTROL_SESSION action=arm` is required before
  allowlisted host TX
- `bus=0 id=0x503` host request -> `CONTROL_ACK` then `CAN_TX_RAW bus=0`
- `bus=1 id=0x503` host request -> `CONTROL_ACK` then `CAN_TX_RAW bus=1`
- `CONTROL_ACK status=1` is never treated as final CAN success; matching
  `CAN_TX_RAW` remains required
- heartbeat timeout blocks new host TX and appears in `BOARD_HEALTH v2`
- automated HIL gate `pc_tools/hil_csm_dual_safety_gate.py` must pass before
  calling the CSM control gateway complete
- Kvaser sees the `bus=1` audited frame and reports zero error frames
- production runtime env has periodic test TX disabled; smoke TX belongs only
  to `portenta_h7_m7_mid_mcp2515_j4_dual_smoke`

## CSM Regression - mid_dual_can
- deferred target, not current CSM default
- `CAPABILITY` profile major 2 exposes two bus descriptors
- bus0 descriptor: Mid J14 CAN0 + ADA-5708/TJA1051, monitor/system role
- bus1 descriptor: Mid J4 terminal CAN1 through onboard U2, drive/control role
- MCP2515 dependency is absent from this deferred dual-internal-CAN build path
- ArduinoCore single-CAN limitation is reported until an internal CAN0 backend exists

## VMS Regression
- serial open does not imply board alive
- typed frame parser rejects bad CRC and resyncs
- exact typed bytes are stored append-only
- replay preserves source order and record type
- `CONTROL_ACK` and `CAN_TX_RAW` display as separate events
- legacy 20-byte import remains offline compatibility only

## Hardware Test Regression
- Hardware setup uses `board/docs/HARDWARE_BRINGUP_GATE_HARNESS_KO.md`.
- Before every hardware test, state one test intent, pass evidence, and fail branch.
- Before repeating a failed hardware test, state confirmed facts, unconfirmed
  hypothesis, what changed since the previous failed run, and expected evidence.
- Do not repeat a test with the same wiring, firmware, PCAN state, and
  measurement point.
- Do not move from SPI/register failure to CAN/PCAN reasoning until controller
  register access is proven.
- Completion reports must include failed/repeated test count, confirmed facts,
  remaining unconfirmed hypotheses, final firmware env, and RX/TX/analyzer
  evidence.
