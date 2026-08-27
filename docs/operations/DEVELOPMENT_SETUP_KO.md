# CSM Development Setup

Authority: `TOOLCHAIN / BUILD ROUTES`

설치나 global PATH 변경 없이 기존 deterministic executable을 사용한다. 정식 PlatformIO
project는 `firmware/csm`이고 active environment는 platformio.ini가 소유한다.

## Existing Tool Route

```powershell
$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'
& $pio --version
```

## Harness and Static Guards

```powershell
python tools/verify_harness.py
python firmware/csm/tools/product_invariant_guard.py
python firmware/csm/tools/architecture_conformance_guard.py
python firmware/csm/tools/experiment_guard.py
python firmware/csm/tools/wifi_architecture_guard.py
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/csm/tools/run_remote_control_contract_test.ps1
```

## Exact Builds

```powershell
$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'
& $pio run -d firmware/csm -e portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi
& $pio run -d firmware/csm -e portenta_h7_m4_remote_frontend
```

platformio.ini에 없는 과거 env 이름을 build route로 사용하지 않는다.

## Compile Database

compile DB는 product authority가 아니라 특정 environment에서 생성된 analysis input이다.
active Service/HIL M7 DB 생성법:

```powershell
$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'
& $pio run -d firmware/csm -e portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi -t compiledb
```

생성된 `firmware/csm/compile_commands.json`은 environment/source filter/defines가 위 env와
일치할 때만 사용하고 Git에 commit하지 않는다. 다른 env의 DB는 즉시 stale로 취급해
삭제·재생성한다. M4 분석에는 M4 env로 별도 생성해야 하며 두 core DB를 하나의 truth처럼
혼합하지 않는다.

## Claim and Hardware Gate

- build success는 upload/device/CAN/HIL 성공이 아니다.
- upload 전 target Portenta/core/artifact, transceiver power, wiring/termination과 차량 motion
  safe state를 확인한다.
- hardware write와 external CAN transmit은 명시 승인 없이는 실행하지 않는다.
