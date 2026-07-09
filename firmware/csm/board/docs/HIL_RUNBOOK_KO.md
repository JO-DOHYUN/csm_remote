# HIL_RUNBOOK_KO

## Minimum Verification
- CAN flood must not create hidden drops. Drops must appear in counters/events.
- Encoder pulse injection must expose miss, overflow, index, and fault evidence.
- ADC bursts must not starve CAN evidence.
- Reconnect, lease timeout, estop, and field power loss must be visible.
- Long soak must show no unreported drift, overflow, or queue growth.

## CSM Safety-Gated HIL Addendum, 2026-05-18
- Build env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm`.
- Automated gate: `py -3 pc_tools/hil_csm_dual_safety_gate.py --port COM7 --baud 115200`.
- Hardware:
  - bus0 MCP2515/TJA1050, 8 MHz crystal, 500 kbps, Classic CAN 2.0.
  - bus1 Mid Carrier J4/U2, 500 kbps, Classic CAN 2.0.
- Boot pass evidence:
  - `CAPABILITY` length 112, profile major 3.
  - bus0 descriptor backend MCP2515, bus1 descriptor backend ArduinoCAN.
  - bus descriptor role is `0` or non-authoritative hint only.
  - `BOARD_HEALTH` length 192, health version 4, heartbeat/session counters and
    bus별 RX/drop/queue counters visible.
- Reject gate:
  - Send `HOST_CAN_TX_REQUEST` before heartbeat and arm.
  - Expected: `CONTROL_ACK status=0 reason=9` or `10`.
  - Expected: no matching `CAN_TX_RAW`.
- Arm gate:
  - Send `HOST_HEARTBEAT`.
  - Send `HOST_CONTROL_SESSION action=1 lease_ms=500 requested_bus=0xFF`.
  - Expected: `CONTROL_ACK status=1 reason=0`.
  - Expected: safety state becomes armed or control active.
- TX audit:
  - Send allowlisted standard ID `0x503` on bus0 and bus1.
  - Expected per bus: `CONTROL_ACK status=1 reason=0`, then matching `CAN_TX_RAW`.
  - External analyzer must see the same frame on the selected physical channel.
- Timeout:
  - Stop heartbeat for more than 300 ms.
  - Expected: new host TX is rejected.
  - Expected: `BOARD_HEALTH v4` heartbeat age increases.
  - Expected: no hidden CAN write occurs.
