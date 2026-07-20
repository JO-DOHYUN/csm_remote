# CSM 개발 환경

## PC

- Windows PowerShell
- Git
- Python 3
- PlatformIO Core
- Portenta H7 USB driver/serial access
- Android/Windows VSM은 각 저장소의 toolchain을 사용한다.

## 기준 명령

```powershell
python tools/verify_harness.py
python firmware/csm/tools/remote_phase1_guard.py
python firmware/csm/tools/remote_phase2a_guard.py
python firmware/csm/tools/remote_phase2b_guard.py
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m4_remote_frontend_build_proof
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m4_remote_serial3_capture_probe
```

## 하드웨어 gate

upload 전에는 연결된 Portenta, M4/M7 target, 차량/bench CAN, transceiver power, RC receiver 전원을 확인한다. production Wi-Fi module과 library는 코드 도입 전에 실제 보드에서 firmware/driver/API 가용성을 확인한다.

Qt 라이선스나 Android toolchain은 CSM firmware build에 필요하지 않다. CSM protocol은 앱 구현 언어와 독립적인 binary contract다.
