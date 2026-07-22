# CSM Remote

Portenta H7 기반 CSM 제품 펌웨어 저장소다. 최종 제품은 RC를 최우선 제어원으로 유지하면서 Windows VSM에는 USB CDC, VSM Android에는 Wi-Fi로 동일한 typed 관측 stream을 제공한다.

- 작업 시작: `START_HERE_KO.md`
- 현재 상태: `BRIEF.md`
- 펌웨어: `firmware/csm`
- canonical wire: `firmware/csm/shared/docs/TRANSPORT_AND_RECORDS_KO.md`

```powershell
python tools/verify_harness.py
python firmware/csm/tools/remote_phase1_guard.py
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/csm/tools/run_remote_control_contract_test.ps1
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m4_remote_frontend
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m7_mid_mcp2515_j4_remote_product_wifi
```

빌드는 하드웨어 동작 증명이 아니다. upload와 HIL은 안전한 bench 조건에서 별도 수행한다.
