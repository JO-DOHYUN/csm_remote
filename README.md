# CSM Remote Workspace

This workspace designs and implements RC remote-control capability on top of the
existing CSM firmware baseline.

## Codex Entry
Codex-facing routing starts at `AGENTS.md`.

Required path:

```text
AGENTS.md
-> README.md
-> docs/remote/AGENTS.md
-> task-matched product, architecture, review, or firmware documents
```

Do not start from the old flat root document layout. Remote documents now live
under `docs/remote/**` by role.

## Current Baseline
```text
Workspace: C:\WORKS\VS\csm_remote
Imported CSM subtree: firmware/csm
Original CSM upstream reference: C:\Users\JEON0295\Documents\PlatformIO\Projects\J_ArdP7_AM2_CSM
CSM baseline commit: bfef287 Finalize passive CSM fault hold evidence
VSM reference: C:\WORKS\VS\turn81_full_buildfix2
Target hardware: Portenta H7 + Mid Carrier + Radiolink T16D + R16SM
```

## Product Authority
1. `docs/remote/product/PRODUCT_DEFINITION_KO.md`
2. `docs/remote/product/OPEN_DECISIONS_KO.md`
3. `docs/remote/product/DECISION_LEDGER_KO.md`
4. `docs/remote/product/REQUIREMENT_TRACE_KO.md`
5. `docs/remote/architecture/**`
6. `docs/remote/reviews/**`
7. Imported CSM documents under `firmware/csm/**`

## Router Documents
- `docs/remote/AGENTS.md`: remote product/document router.
- `docs/remote/harness/ROUTING_MATRIX_KO.md`: token-efficient task routing matrix.
- `docs/remote/harness/DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md`: development harness philosophy and gates.
- `firmware/AGENTS.md`: firmware tree router.
- `firmware/csm/AGENTS.md`: imported CSM firmware implementation router.

## Firmware Path
```text
CSM PlatformIO project: firmware/csm
M4 build proof env: portenta_h7_m4_remote_frontend_build_proof
M4 Serial3 capture probe env: portenta_h7_m4_remote_serial3_capture_probe
```

Build from the workspace root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
```

## Core Rule
```text
No confirmed autonomy release = zero local CAN TX.
```
