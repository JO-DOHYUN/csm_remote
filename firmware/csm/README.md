# HAMT2 CSM Firmware

Standalone PlatformIO repository for the HAMT2 CSM board firmware.

Do CSM build and upload work from this folder:

```text
C:\Users\JEON0295\Documents\PlatformIO\Projects\J_ArdP7_AM2_CSM
```

VSM/Qt and Android app work belong in separate repositories. Do not place or
build nested app workspaces under this CSM folder.

## Active Target

- Board: Portenta H7 M7 + Mid Carrier ASX00055
- env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm`
- `bus0`: external MCP2515/TJA1050, Classic CAN 2.0 500 kbps
- `bus1`: Mid Carrier J4 CAN1/U2, Classic CAN 2.0 500 kbps
- Live stream: typed transport v1
- High-load RX: `CAN_RX_SEGMENT`
- TX evidence: `CONTROL_ACK` is board decision, `CAN_TX_RAW` is actual CAN write audit

## Build

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm
```

## Upload

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm -t upload
```

## Verify Firmware Identity

```powershell
py -3 pc_tools\verify_typed_stream.py --port COM7 --seconds 4 --max-records 20
```

`CAPABILITY` must show the expected env, git SHA, dirty flag, MCP SPI speed,
IRQ mode, and drain budget.

## Contracts

`shared/docs/TRANSPORT_AND_RECORDS_KO.md` is the wire-contract source of truth
for CSM/VSM typed records.
