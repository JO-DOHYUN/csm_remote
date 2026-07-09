# BUILD_VERIFY_POLICY_KO

## CSM Build
Default product CSM verification:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

This defaults to `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`.

Full Instrumented bench/HIL compile verification:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_full_instrumented
```

회귀 검증:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_dual_can_basic
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mcp_int_main
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mcp_polling_recovery
```

Mid Carrier migration compile gate:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e portenta_h7_m7_mid_dual_can20
```

This build proves only that the MCP-free production profile compiles. It does
not prove CAN0, CAN1, ADA-5708, Mid Carrier U2, termination, or physical bus
success.

## HIL 구분
- Build PASS: 컴파일 가능 evidence.
- Upload PASS: 보드 flash 가능 evidence.
- Stream decode PASS: USB typed stream evidence.
- CAN analyzer PASS: 실제 CAN bus evidence.

빌드 성공만으로 하드웨어 성공을 주장하지 않는다.
Passive Product 빌드 성공만으로 `verified_passive`를 주장하지 않는다.
`CAPABILITY passive_acceptance_allowed=1`은 hardware safety case와 bench
verification ID가 모두 있는 경우에만 허용한다.

## VMS Build
이 repo에는 Qt reference/source가 없을 수 있다. Qt 작업 repo에서는 해당 repo의
CMake preset과 tests를 기준으로 검증한다.
