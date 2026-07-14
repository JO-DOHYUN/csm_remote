# AGENTS.md - Firmware Router

## Scope
This file routes work under `firmware/**`.

The workspace root `AGENTS.md` remains the top authority for CSM Remote product
decisions. Firmware-local routers define implementation constraints only.

## Routing
- Active firmware project: `firmware/csm`.
- For CSM work, read `firmware/csm/AGENTS.md`.
- Do not create sibling `vsm`, `android`, or duplicate `csm` workspaces under
  `firmware/**`.

## Build Location
From the workspace root, build CSM with:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
```
