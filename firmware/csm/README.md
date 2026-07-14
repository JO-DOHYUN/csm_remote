# HAMT2 CSM Firmware

Imported PlatformIO firmware project for the CSM Remote workspace.

Active workspace path:

```text
C:\WORKS\VS\csm_remote\firmware\csm
```

Original upstream reference:

```text
C:\Users\JEON0295\Documents\PlatformIO\Projects\J_ArdP7_AM2_CSM
```

Do not treat the upstream path as the active build root while working in
`csm_remote`.

## Active Target
- Board: Portenta H7 M7 + Mid Carrier ASX00055
- Product env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`
- M4 proof env: `portenta_h7_m4_remote_frontend_build_proof`
- M4 Serial3 capture probe env: `portenta_h7_m4_remote_serial3_capture_probe`
- Remote Phase 1: deny-first skeleton, not wired to runtime M4 UART/IPC/CAN TX
- `bus0`: external MCP2515/TJA1050, Classic CAN 2.0 500 kbps
- `bus1`: Mid Carrier J4 CAN1/U2, Classic CAN 2.0 500 kbps
- Live stream: typed transport v1
- TX evidence: `CONTROL_ACK` is board decision, `CAN_TX_RAW` is actual CAN write audit

## Build
From the workspace root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
```

From this directory:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
```

M4 remote frontend build proof from the workspace root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m4_remote_frontend_build_proof
```

M4 Serial3 capture probe from the workspace root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m4_remote_serial3_capture_probe
```

## Verify
Remote Phase 1 guard from workspace root:

```powershell
python firmware/csm/tools/remote_phase1_guard.py
python firmware/csm/tools/remote_phase2a_guard.py
python firmware/csm/tools/remote_phase2b_guard.py
```

Wire contract:

```text
shared/docs/TRANSPORT_AND_RECORDS_KO.md
```
