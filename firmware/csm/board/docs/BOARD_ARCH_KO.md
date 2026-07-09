# BOARD_ARCH_KO

## 최종 판단
현재 펌웨어는 `src/main.cpp` 기준으로 raw CAN 수신을 20바이트 고정 패킷으로 USB Serial에 올리는 장치다. 이 기준선은 빌드 가능한 bring-up 자산으로 유지한다.

최종 보드 재설계 방향은 단순 CAN 브리지나 로거가 아니라, `shared/docs/*` 계약을 만족하는 증거 수집/제어 감사 보드다. 핵심 목표는 센서 원신호, 하위제어기 CAN 보고값, host 제어명령, 관측계 손실을 같은 시간축에서 구분 가능하게 남기는 것이다. 현재 20B stream은 양산 기본안이 아니라 bring-up/호환 모드로만 둔다.

## 범위
- 이 문서는 현재 저장소의 보드 설계 기준이다.
- QT 쪽 구현은 `qt/` 기준 문서와 shared 계약만 참조하고, 이 보드 작업에서 직접 수정하지 않는다.
- `shared/docs/TRANSPORT_AND_RECORDS_KO.md`의 typed record 목록을 기준 계약으로 둔다.
- 현재 20B stream은 호환/bring-up 모드이며, 양산 기본 transport는 framed transport + typed records다.
- 최종 carrier board 구상, 전원, CAN, ADC, 안전 핀맵은 `board/docs/HARDWARE_FINAL_CONCEPT_KO.md`를 따른다.
- 구동 엔코더 입력 회로와 핀 지정은 `board/docs/ENCODER_DBS60E_INPUT_KO.md`를 따른다.

## 확정 구조
- `CanRxLane`: 하위제어기에서 들어오는 CAN 원문을 `CAN_RX_RAW`로 캡처한다.
- `CanTxAuditLane`: 보드가 실제 송신한 CAN 원문을 `CAN_TX_RAW`로 캡처한다. host 요청과 실제 TX는 같은 것으로 취급하지 않는다.
- `EncoderEdgeLane`: DBS60E 구동 엔코더의 A/B/Z 원신호, index, overflow, fault를 `ENC_EDGE_RAW`로 캡처한다.
- `EncoderDerivedLane`: edge raw에서 계산한 속도/방향/누락 추정값을 `ENC_DERIVED`로 별도 발행한다.
- `AnalogSampleLane`: raw 전압 또는 디버그 burst를 `ADC_SAMPLE`로 발행한다. CAN frame으로 위장하지 않는다.
- `ControlLane`: host command를 검증하고 수락/거절 결과를 `CONTROL_ACK`로 남긴다.
- `SafetySupervisor`: `MONITOR_ONLY`, `CONTROL_STANDBY`, `CONTROL_ARMED`, `FAULT`, `ESTOP` 전이를 관리한다.
- `UplinkScheduler`: lane별 큐를 typed record stream으로 전송하고, debug 데이터가 CAN/health/control 증거를 밀어내지 않도록 우선순위를 적용한다.
- `HealthMonitor`: drop, overflow, malformed input, bus fault, timeout, time sync 상태를 `BOARD_HEALTH`와 `BOARD_EVENT`로 발행한다.

## 시간축
- 보드 내부 기준 시간은 `mono64` 하나로 통일한다.
- 권장 단위는 microsecond이며, 현재 `micros()` 32비트 값은 rollover 확장 래퍼를 거쳐 `uint64_t`로 승격한다.
- 모든 raw/derived/control/health record는 같은 `mono64`를 갖는다.
- QT replay와 evidence view는 이 `mono64`를 1차 정렬축으로 사용한다.

## 전송/레코드 방향
- 링크 계층은 framed transport로 전환한다.
- 데이터 계층은 typed record로 전환한다.
- legacy 20B telemetry는 다음 조건에서만 유지한다.
  - 기존 PC 도구와 bring-up 호환이 필요할 때
  - typed record parser가 QT 쪽에 준비되기 전
  - 필드 의미가 바뀌지 않았고, stats/drop 표시가 보존될 때
- typed record 최소 세트는 shared 계약과 동일하다.
  - `CAN_RX_RAW`
  - `CAN_TX_RAW`
  - `ENC_EDGE_RAW`
  - `ENC_DERIVED`
  - `ADC_SAMPLE`
  - `CONTROL_ACK`
  - `BOARD_EVENT`
  - `BOARD_HEALTH`

## 큐와 우선순위
- lane별 입력 큐와 전송 큐를 분리한다.
- CAN RX raw와 health/control event는 debug ADC burst보다 높은 우선순위다.
- queue full은 조용히 폐기하지 않는다. 최소한 lane, 시간, 누적 counter, event reason을 남긴다.
- high-rate lane은 budget을 갖고 drain한다. 한 lane이 loop를 독점하면 결함으로 본다.
- dropped counter만 있고 event나 시간 범위가 없으면 최종 구조에서는 불충분하다.

## 제어 안전
- 기본 상태는 `MONITOR_ONLY`다.
- reconnect 후 자동 arm은 금지한다.
- `CONTROL_ARMED` 조건은 명시 arm, 유효 lease, heartbeat 정상, health 정상이다.
- heartbeat/lease timeout은 neutral 또는 output-off 정책으로 즉시 fallback한다.
- estop은 별도 최상위 차단 경로이며 stale host packet으로 우회할 수 없어야 한다.
- 감사 체인은 항상 복원 가능해야 한다.
  1. host intent 수신
  2. board accept/reject
  3. actual CAN TX raw
  4. ack/event

## 현재 코드와의 차이
- 현재 `src/main.cpp`는 `uint32_t t_us = micros()` 기반이므로 아직 `mono64`가 아니다.
- 현재 CAN RX path는 raw CAN과 stats를 20B packet으로만 내보낸다.
- 현재 `can_fifo_overflow_total`, `err_passive_1s`, `bus_off_1s`는 API 한계로 placeholder다.
- 현재 firmware에는 encoder, ADC burst, host control, safety state machine이 아직 없다.
- 따라서 이번 최종안은 코드가 이미 완성됐다는 선언이 아니라, 다음 구현 단계의 계약과 검증 기준을 확정하는 문서다.

## 구현 순서
1. 현재 20B CAN RX baseline을 legacy compatibility 기준으로 보존한다.
2. `mono64` timebase 래퍼와 boot/capability record를 추가한다.
3. `BOARD_HEALTH` / `BOARD_EVENT`로 drop/overflow/time fault를 typed record로 보이게 한다.
4. `CAN_RX_RAW`를 기본 typed record로 발행한다.
5. `EncoderEdgeLane`과 `ENC_DERIVED`를 추가하되 raw/derived를 분리한다.
6. `AnalogSampleLane`을 debug burst로 추가하고 scheduler budget으로 CAN path를 보호한다.
7. `ControlLane`과 `SafetySupervisor`를 확장한 뒤 CAN TX audit chain을 닫는다.
8. HIL runbook 기준으로 flood, pulse injection, ADC burst, reconnect, estop, soak을 검증한다.

## 수락 기준
- build 성공 여부를 실제 명령 결과로 기록한다.
- physical capture 성공은 hardware smoke test 없이는 주장하지 않는다.
- source 구분이 QT evidence view에서 혼동되지 않아야 한다.
- hidden drop, overflow, timeout, malformed input은 결함이다.
- schema나 timing semantics 변경 시 shared 문서를 같은 턴에서 갱신한다.
