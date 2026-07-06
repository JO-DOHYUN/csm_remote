# BRIEF.md - CSM Repository Brief

## Current Focus
This is the standalone CSM board firmware repository.

- CSM firmware lives at the repository root, `src/`, `include/`, and `board/`.
- Shared CSM/VSM wire contracts live under `shared/`.
- VSM/Qt and Android app work must remain in separate repositories.
- Build and upload from this directory only:
  `C:\Users\JEON0295\Documents\PlatformIO\Projects\J_ArdP7_AM2_CSM`.

## Current Build Baseline
- Board: Arduino Portenta H7 M7 + Mid Carrier ASX00055.
- Product default env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`.
  This is the two-bus ACK-capable observe-only passive product artifact for the current vehicle use
  case.
- Final completion target: `board/docs/CSM_FINAL_PRODUCT_COMPLETION_TARGET_KO.md`.
- Bench/HIL full env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm_full_instrumented`.
- Kvaser/PCAN single-node transmit checks require the Passive Product host
  session to be open and ACK-observe enabled. ACK capability is not host
  TX/control capability.
- Current board identity before this cleanup was read on COM7 as:
  - `git=4d21a3c9431`
  - `dirty=1`
  - `env=portenta_h7_m7_mid_mcp2515_j4_dual_csm`
  - `BOARD_CAN_IRQ_MODE=0`
  - `BOARD_MCP2515_SPI_HZ=8000000`
  - `BOARD_CAN_SERIAL_DRAIN_BUDGET=512`
- This repository now carries that uploaded source state so future uploads no
  longer depend on `C:\WORKS\VS\csm_zip_pre_wifi`.

## CSM Baseline
- Passive Product `bus=0`: external MCP2515/TJA1050, 8 MHz crystal, Classic
  CAN 2.0 500 kbps. In the field build, MCP SPI/reset/bitrate setup is deferred
  through USB power-up and runs only after stable CDC/DTR session plus quiet
  window; then ACK-observe is enabled.
- Passive Product `bus=1`: Mid Carrier J4/U2 built-in CAN, Classic CAN 2.0
  500 kbps. Built-in CAN setup follows the same deferred-init rule and enters
  ACK-observe only after stable session plus quiet window.
- Host downlink/control/TX/test paths are compile-time removed in the product
  passive env. Encoder-derived streaming is also disabled in the product passive
  env; keep non-CAN instrumentation in Full/diagnostic envs.
- Full Instrumented keeps `bus=0` MCP2515/TJA1050 and `bus=1` Mid Carrier J4
  CAN1 through onboard U2 for bench/HIL audited control tests.
- Live production output is typed transport v1.
- High-load receive uses `CAN_RX_SEGMENT` while preserving per-frame truth.
- `CONTROL_ACK` is request evidence only in Full Instrumented; final CAN write
  evidence is `CAN_TX_RAW`. Passive Product must not advertise either as a live
  active capability.
- `CAPABILITY` exposes firmware identity, bus descriptors, and build settings.
- `CAPABILITY v5` exposes firmware profile, vehicle-impact state, host command
  RX, control path, USB reset sensitivity, CDC DTR session policy, and passive
  safety evidence IDs.
- `BOARD_HEALTH v7` preserves earlier offsets and adds uplink pool/descriptor,
  CAN RX task max time, USB reconnect/reset counters, and passive violation
  latch diagnostics plus host-absent discard, MCP passive readback, TXREQ
  violation, and DTR change counters.
- Passive CDC uplink is session-gated: before host CDC/DTR session open, CAN
  front-end initialization and typed payload staging are both held. Session open
  clears stale payloads, emits session evidence, waits the quiet window, then
  initializes CAN front ends and enables ACK-observe for new frames only.
- USB attach quarantine is CDC/uplink/session cleanup only. It must never replay
  old CAN payload or enable host TX/control. The controlled transition to
  ACK-observe is allowed only after deferred CAN front-end initialization
  succeeds.
- USB physical hotplug disturbance is not considered solved by CDC/uplink
  quarantine alone. Firmware must expose lifecycle evidence, and final
  vehicle-impact-free PASS requires external analyzer/scope/DTC proof.
- `CAPABILITY v6` hardware fields are runtime claims/artifact references only.
  Final `verified_passive` requires external analyzer/scope/DTC evidence whose
  IDs match the claim fields.
- The product remains two-bus. One-bus passive products are invalid, but
  one-bus/missing-bus mismatch diagnostics must remain visible to VSM.

## Canonical Contracts
- Root routing: `AGENTS.md`
- Board scoped rules: `board/AGENTS.md`, `board/BRIEF.md`
- Wire contract: `shared/docs/TRANSPORT_AND_RECORDS_KO.md`
- HIL runbook: `board/docs/HIL_RUNBOOK_KO.md`

## Verification Commands
Use these commands only when the changed surface requires them. For docs,
harness, or comment-only changes, run `git diff --check` and targeted search
instead of PlatformIO. Upload is a separate hardware action, not a build proof.

Build Passive Product firmware:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
```

This is the only CSM field/product build name for the current hardware. Do not
build lab/full/test envs unless the task is explicitly a bench diagnostic.

Upload Passive Product firmware only when the vehicle/bench context is safe for
MCU reset and USB re-enumeration:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive -t upload
```

Build Full Instrumented firmware:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_full_instrumented
```

Decode board identity after upload:

```powershell
py -3 pc_tools\verify_typed_stream.py --port COM7 --seconds 4 --max-records 20
```

## Immediate Next Work
- Use this repository root for all CSM PlatformIO build/upload work.
- Keep VSM parser updates in the standalone VSM repository.
- Build only the affected CSM env unless profile separation or wire
  compatibility is part of the change.
- Upload only on explicit request and safe hardware context.
- Do not commit build outputs, captures, archives, or nested app workspaces.
