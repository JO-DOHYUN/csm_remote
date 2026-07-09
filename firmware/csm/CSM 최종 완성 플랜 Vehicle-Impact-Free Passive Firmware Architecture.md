# CSM 최종 완성 플랜: Vehicle-Impact-Free Passive Firmware Architecture

## Summary
CSM 최종 목표는 “잘 기록하는 보드”가 아니라 **USB/VSM/UI/GW/디버그 상태가 어떻게 변해도 차량 CAN에 영향을 주지 않는 passive CAN evidence device**다.

현재 기준 CSM은 기본 env에서 host CAN TX, control TX, USB reconnect reset, MCP normal mode, host downlink parser가 살아 있으므로 실차 passive 제품 기준으로는 미완성이다. 최종 구조는 하나의 공통 core를 공유하되, 실행 산출물은 `Passive Product`와 `Full Instrumented`로 명확히 분리한다.

## Product Profiles
- `CSM Passive Product`
  - 실차 기본/출고용.
  - CAN listen-only 또는 hardware-silent.
  - CAN TX, ACK 참여, error frame 생성, host command write path, control, heartbeat, lab gateway 전부 불가능해야 한다.
  - USB 연결/해제/재연결/reset/backpressure가 CAN RX task와 transceiver 상태에 영향을 주면 실패.
- `CSM Full Instrumented`
  - bench/HIL/개발용.
  - host TX/control/debug 가능.
  - 항상 `vehicle_impact_capability=possible`, `passive_acceptance_allowed=false`로 광고한다.
  - 실차 passive PASS 근거로 사용 금지.

## Architecture Skeleton
```text
Vehicle CAN
  -> Passive CAN Frontend
       CAN RX ISR/task
       fixed CAN RX ring
  -> Telemetry/Uplink Plane
       segment builder
       typed encoder
       USB CDC/Bulk/WiFi transport
  -> VSM capture.stream/index

Optional:
  Debug Plane       default off, bounded, CAN RX와 pool 공유 금지
  Active-Test Plane full profile only, passive build에는 미포함
```

소유권 원칙:
- CAN RX ISR/task는 CAN RX ring만 쓴다.
- CAN RX ISR/task에서 USB 상태 확인, malloc/free, string/JSON, serial write, blocking push 금지.
- Uplink가 막히면 telemetry만 drop/invalid 처리한다. 차량 CAN 수신 타이밍에 영향 주면 실패.
- Debug/control/uplink pool은 CAN RX ring pool과 분리한다.

## Phase 0. Baseline Freeze
- 현재 flashed firmware hash/env/profile을 확인할 수 있는 build identity를 CAPABILITY에 넣는다.
- `platformio.ini` default env가 active-capable이면 실차용 default에서 제외한다.
- 현재 active 위험 경로를 static scan으로 고정한다:
  - `BOARD_ENABLE_HOST_CAN_TX*`
  - `BOARD_*CONTROL_TX_ALLOWED`
  - `setNormalMode`
  - `NVIC_SystemReset`
  - `HostDownlinkParser`
  - `Serial.write`/`send_nb`
  - TX test path
- 이 scan이 passive env에서 하나라도 검출되면 build 실패.

## Phase 1. Passive Build Profile 신설
- 새 env를 만든다:
  - `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`
- passive build flags:
  - `BOARD_CSM_PROFILE_PASSIVE_PRODUCT=1`
  - `BOARD_ENABLE_HOST_CAN_TX=0`
  - `BOARD_ENABLE_HOST_CAN_TX_BUILTIN=0`
  - `BOARD_ENABLE_HOST_CAN_TX_MCP2515=0`
  - `BOARD_MCP2515_CONTROL_TX_ALLOWED=0`
  - `BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED=0`
  - `BOARD_ENABLE_MCP2515_TX_TEST=0`
  - `BOARD_ENABLE_BUILTIN_CAN_TX_TEST=0`
  - `BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT=1`
  - `BOARD_USB_CDC_RECONNECT_RESET_MS=0`
- passive env에서 active-test/control/downlink source는 compile-out한다. 런타임 if문으로만 막는 방식 금지.
- Full env는 별도 이름으로 유지하고 실차 기본값으로 쓰지 않는다.

## Phase 2. Passive CAN Frontend
- MCP2515는 passive env에서 반드시 listen-only로 시작하고, TX 시도 때문에 normal mode로 전환하는 코드가 없어야 한다.
- Builtin CAN도 passive env에서는 TX/control path compile-out한다.
- reset 중 transceiver safe 상태를 보장한다:
  - TXD safe pull
  - STB/Silent default-safe
  - MCU reset 동안 transceiver standby/silent
- CAPABILITY에 bus별로 명확히 광고한다:
  - `bus_mode=listen_only|hardware_silent|normal`
  - `tx_capability=false`
  - `control_tx_allowed=false`
  - `ack_capability=false 또는 verified status`
  - `error_frame_capability=false 또는 verified status`
  - `transceiver_reset_safe=true/false`
  - `hardware_safety_case_id`

## Phase 3. CAN RX Ring 절대 우선 구조
- CAN RX ISR/task의 책임은 fixed ring append와 counter 증가뿐이다.
- CAN RX ring full이면 `can_rx_ring_overrun_total`만 증가시키고 즉시 복귀한다.
- CAN RX task 금지 항목:
  - typed frame encoding
  - USB TX 호출
  - debug event 생성
  - host command 처리
  - dynamic allocation
  - queue wait/retry
- 측정 counter:
  - `can_rx_isr_max_us`
  - `can_rx_task_max_us`
  - `can_rx_ring_used/max/capacity`
  - `can_rx_ring_overrun_total`
  - `mcp_rx_fifo_overflow_total`
  - `capture_seq64_gap_source`

## Phase 4. Uplink 구조 재설계
- 기존 단일 serial TX ring + clear 정책은 제거한다.
- 최종 구조는 `payload pool + priority descriptor queue`다.
  - payload pool: fixed block/slab, 동적 증가 없음.
  - descriptor: `{record_type, flags, pool_id, offset, length, priority, seq_hint}`
  - typed seq/CRC는 송출 직전에 붙인다.
- priority:
  - P0: CAPABILITY, fatal BOARD_EVENT, loss/invalid marker
  - P1: CAN_RX_SEGMENT
  - P2: BOARD_HEALTH summary
  - P3: CAN_TX_RAW/CONTROL_ACK, full profile only
  - P4: repeated MCP error/status
  - P5: debug/profiler
- backpressure 정책:
  - `txRing.clear()` 금지.
  - CAN_RX_SEGMENT가 pool 부족으로 못 올라가면 `can_segment_enqueue_fail_total`과 `telemetry_loss_total`을 증가시키고 fatal diagnostic을 남긴다.
  - debug/profiler/repeated status는 먼저 coalesce/drop한다.
  - silent drop 금지.
- segment payload 목표:
  - typed frame 전체가 HS 512B packet에 맞도록 `CAN_RX_SEGMENT` max frame을 15 이하 또는 byte-limit 기준으로 제한한다.
  - flush 조건: max frame, max byte, max wait 1~2ms.

## Phase 5. USB/Host 상태와 CAN 분리
- USB connected/disconnected 상태가 CAN RX frontend에 영향을 주면 실패.
- passive env에서 USB disconnect watchdog reset 제거.
- reconnect가 필요하면 uplink plane만 reset하고 CAN controller/transceiver mode는 유지한다.
- USB backpressure는 아래 counter로만 남긴다:
  - `uplink_queue_used/max`
  - `uplink_pool_used/max`
  - `uplink_backpressure_total`
  - `uplink_drop_debug_total`
  - `uplink_drop_status_total`
  - `uplink_drop_can_segment_total`
  - `capture_invalid_reason`
- USB host가 없을 때도 CAN RX ring append timing은 동일해야 한다.

## Phase 6. Host Downlink/Control 분리
- passive env:
  - `HostDownlinkParser` compile-out 또는 read-and-discard only.
  - `HOST_CAN_TX_REQUEST`, `HOST_HEARTBEAT`, `HOST_CONTROL_SESSION`, `HOST_CLEAR_FAULT`는 처리 금지.
  - CONTROL_ACK도 “명령 처리 증거”이므로 passive에서는 downlink query에 대한 최소 reject 외에는 생성하지 않는다.
- full env:
  - HostDownlinkParser 유지.
  - SafetySupervisor + heartbeat + lease + allowlist + CAN_TX_RAW audit 필수.
  - `CONTROL_ACK ACCEPTED`는 CAN TX 성공이 아니며, `CAN_TX_RAW`만 실제 TX 성공이다.
- passive에서 active symbol이 링크되면 build 실패하도록 guard를 둔다.

## Phase 7. Capability Manifest V2
- CSM CAPABILITY를 profile/capability manifest로 확장한다.
- firmware hot path에는 JSON 금지. compact binary/TLV 확장으로 전송한다.
- 필수 필드:
  - `firmware_profile`
  - `firmware_build_id`
  - `firmware_git_sha`
  - `profile_lock_state`
  - `bus_mode`
  - `tx_capability`
  - `control_path`
  - `host_command_rx`
  - `usb_backpressure_isolated`
  - `dtr_reset_sensitive`
  - `transceiver_reset_safe`
  - `hardware_safety_case_id`
  - `bench_verification_id`
- VSM은 이 값을 사람이 읽는 report/JSON으로 변환한다.

## Phase 8. Debug Plane 분리
- passive debug는 CSM COM을 새로 열거나 write하지 않는다.
- CSM firmware debug는 기본 OFF, bounded, low priority다.
- debug event pool은 CAN RX/uplink CAN segment pool과 분리한다.
- repeated MCP error는 rate-limit/coalesce한다.
- full lab gateway는 active-test 전용으로 분류하고 passive artifact와 섞지 않는다.

## Phase 9. Board Health/Diagnostics
- BOARD_HEALTH extended payload에 다음을 추가한다:
  - `firmware_profile`
  - `vehicle_impact_state`
  - `can_rx_ring_high_water`
  - `can_rx_task_max_us`
  - `uplink_pool_high_water`
  - `uplink_descriptor_high_water`
  - `uplink_backpressure_total`
  - `can_segment_enqueue_fail_total`
  - `usb_reconnect_count`
  - `usb_forced_reset_count`
  - `passive_violation_latch`
- passive violation latch 조건:
  - any serial downlink command processed
  - any CAN TX function called
  - MCP normal mode entered
  - USB reconnect reset attempted
  - transceiver safe pin not asserted during reset test
- latch는 power cycle 또는 explicit bench command 없이는 지우지 않는다.

## Phase 10. Hardware Safety Gate
- passive product는 software listen-only만으로 최종 PASS 금지.
- 최종 권장 hardware:
  - isolated CAN transceiver
  - TXD physically gated or disconnected in passive variant
  - STB/Silent reset-safe resistor
  - USB/PC side galvanic isolation 검토
  - reset/boot 동안 CANH/CANL dominant glitch 0
- hardware가 아직 active-capable이면 `vehicle_impact_state=configured_passive`까지만 표시하고 `verified_passive` 금지.

## Test Plan
- Static/build:
  - passive env에서 TX/control/downlink/normal-mode/reconnect-reset symbol 검출 0
  - full env는 active 기능이 보이되 `full_instrumented` capability로 광고
  - PlatformIO passive/full 각각 build
- Unit/host parser:
  - typed frame CRC/seq 유지
  - CAPABILITY manifest decode parity
  - passive downlink reject/discard
  - priority descriptor queue ordering/cap/overrun
  - payload pool alloc/release 누수 0
- Bench:
  - USB host 없음/연결/해제/재연결 반복 중 CAN RX task max_us 변화 없음
  - USB TX blocked 상태에서 CAN RX ring latency 변화 없음
  - debug flood 중 CAN RX drop 증가 없음
  - uplink blocked 시 telemetry loss만 증가하고 CAN FIFO overflow 증가 없음
- Electrical:
  - USB plug/unplug 중 CANH/CANL dominant glitch 0
  - reset/boot 중 transceiver silent/standby 유지
  - listen-only/silent mode에서 ACK/error frame 참여 없음 검증
- Vehicle:
  - USB만 꽂고 빼기 반복: ECU error 0
  - VSM connect/disconnect 반복: ECU error 0
  - capture start/stop 반복: ECU error 0
  - passive debug attach/detach: ECU error 0
  - 30분 이상 dual bus capture: vehicle system CAN period error 0

## Acceptance Criteria
- passive firmware는 active CAN TX/control/downlink path가 compile-time으로 제거되어 있다.
- passive CAPABILITY 없이 VSM이 passive PASS를 표시할 수 없다.
- USB/VSM/UI/debug 상태가 CAN RX timing과 transceiver state에 영향을 주지 않는다.
- uplink 손실은 telemetry/capture invalid로만 표시되고 차량 CAN timing 손상으로 이어지지 않는다.
- `serial_ring_clear` 같은 silent truth loss 정책이 없다.
- `CAN_RX_SEGMENT` 손실은 반드시 counter/fatal diagnostic으로 남는다.
- full profile은 bench/HIL 전용이며 실차 passive acceptance에 사용할 수 없다.
- hardware safety case와 bench verification 없이는 `verified_passive`를 선언하지 않는다.

## Non-Negotiable Rules
- passive에서 “UI가 안 보낸다”는 안전 근거가 아니다.
- passive에서 런타임 if문만으로 TX/control을 막는 것은 부족하다.
- passive에서 MCP normal mode 진입은 실패다.
- passive에서 USB disconnect reset은 실패다.
- passive에서 debug gateway가 COM을 소유하면 실패다.
- CAN RX task가 USB/uplink/debug/control 상태를 기다리면 실패다.
- CSM이 만든 CAPABILITY가 실제 hardware state보다 낙관적이면 실패다.

## Definition of Done
CSM 최종 완료는 다음을 모두 만족할 때다.

- `CSM Passive Product`와 `CSM Full Instrumented` 산출물이 명확히 분리된다.
- passive 산출물은 차량 CAN에 TX/dominant/error-frame/ACK 영향을 만들 수 없도록 firmware와 hardware 양쪽에서 검증된다.
- CAN RX frontend, telemetry/uplink, debug, active-test plane이 pool/task/권한 기준으로 분리된다.
- USB backpressure와 VSM drain 지연이 CAN RX task timing에 영향을 주지 않는다.
- capability manifest가 VSM profile과 매칭되어 passive/blocked/full 상태를 정확히 판정한다.
- bench 및 실차 gate를 통과하기 전에는 어떤 UI나 문서도 `vehicle impact impossible`을 최종 PASS로 표시하지 않는다.
