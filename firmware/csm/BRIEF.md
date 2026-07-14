# BRIEF.md - Imported CSM Firmware Brief

## Current Focus
This directory is the imported CSM PlatformIO firmware project used by the CSM
Remote workspace.

- Workspace root: `C:\WORKS\VS\csm_remote`
- CSM firmware path: `firmware/csm`
- Original upstream reference:
  `C:\Users\JEON0295\Documents\PlatformIO\Projects\J_ArdP7_AM2_CSM`
- Imported baseline commit:
  `bfef287 Finalize passive CSM fault hold evidence`

The upstream path is a reference baseline, not the active build root for this
workspace.

## Remote Workspace Relationship
- Remote product decisions come from `../../docs/remote/**`.
- This directory supplies the actual CSM firmware implementation.
- VSM/Qt and Android work remain in their own repositories, but their product
  authority is described by the remote product definition when it affects CSM.
- Imported CSM docs remain useful scoped references; they do not override the
  CSM Remote product definition.

## Current Build Baseline
- Board: Arduino Portenta H7 M7 + Mid Carrier ASX00055.
- Product default env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`.
- Bench/HIL full env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm_full_instrumented`.
- Current Phase 1 remote code is a deny-first skeleton. It compiles into the
  passive build but is not wired to runtime M4 UART, IPC, or vehicle CAN TX.
- Phase 2A M4 build proof env:
  `portenta_h7_m4_remote_frontend_build_proof`. This proves only that selected
  remote parser/normalizer/mailbox-writer code compiles for the M4 target. It
  does not prove Serial3, R16SM, M4 boot coordination, or M4-M7 IPC.
- Phase 2B M4 Serial3 capture probe env:
  `portenta_h7_m4_remote_serial3_capture_probe`. This proves that the M4 target
  can compile a lab-only `Serial3` RX capture path with CRSF parser,
  normalizer, and local mailbox writer. It does not prove actual R16SM baud,
  polarity, physical Mid Carrier pin mapping, or valid hardware decode.

## CSM Baseline
- Passive Product `bus=0`: external MCP2515/TJA1050, 8 MHz crystal, Classic CAN
  2.0 500 kbps.
- Passive Product `bus=1`: Mid Carrier J4/U2 built-in CAN, Classic CAN 2.0
  500 kbps.
- Host downlink/control/TX/test paths are compile-time removed in the product
  passive env.
- Live production output is typed transport v1.
- High-load receive uses `CAN_RX_SEGMENT` while preserving per-frame truth.
- `CONTROL_ACK` is request evidence only. Final CAN write evidence is
  `CAN_TX_RAW` or a later explicitly defined TX evidence record.
- `CAPABILITY` exposes firmware identity, bus descriptors, build settings, and
  passive evidence claims. These claims are not external physical proof.
- Final `verified_passive` requires external analyzer/scope/DTC evidence.
- The product remains two-bus. One-bus passive products are invalid.

## Canonical Contracts
- Workspace routing: `../../AGENTS.md`
- Remote document routing: `../../docs/remote/AGENTS.md`
- CSM firmware routing: `AGENTS.md`
- Board scoped rules: `board/AGENTS.md`, `board/BRIEF.md`
- Wire contract: `shared/docs/TRANSPORT_AND_RECORDS_KO.md`
- HIL runbook: `board/docs/HIL_RUNBOOK_KO.md`

## Verification Commands
Use the smallest proof that covers the changed surface.

Docs/harness-only changes:

```powershell
git diff --check
```

Remote Phase 1 skeleton guard from workspace root:

```powershell
python firmware/csm/tools/remote_phase1_guard.py
```

Remote Phase 2A build-proof guard from workspace root:

```powershell
python firmware/csm/tools/remote_phase2a_guard.py
```

Remote Phase 2B Serial3 capture-probe guard from workspace root:

```powershell
python firmware/csm/tools/remote_phase2b_guard.py
```

Build Passive Product firmware from workspace root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
```

Build M4 Remote Frontend proof firmware from workspace root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m4_remote_frontend_build_proof
```

Build M4 Serial3 capture probe firmware from workspace root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m4_remote_serial3_capture_probe
```

Build Full Instrumented firmware from workspace root when that surface changed:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_full_instrumented
```

Upload only when explicitly requested and when the vehicle/bench context is safe
for MCU reset and USB re-enumeration.

## Immediate Next Work
- Keep remote product definition and firmware implementation in sync.
- Keep VSM parser updates in the standalone VSM repository.
- Do not use old imported CSM prompt documents as product authority without
  checking `../../docs/remote/product/PRODUCT_DEFINITION_KO.md`.
- Do not commit build outputs, captures, archives, or nested app workspaces.
