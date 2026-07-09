# SAFETY_STATE_MACHINE_KO

## 기본 상태
- `MONITOR_ONLY`
- `CONTROL_STANDBY`
- `CONTROL_ARMED`
- `FAULT`
- `ESTOP`

## 핵심 규칙
- 기본 진입은 `MONITOR_ONLY`
- 명시 arm + 유효 lease + 정상 health가 있어야 `CONTROL_ARMED`
- heartbeat 끊김은 즉시 safe fallback
- estop은 별도 최상위 차단 경로
- reconnect 후 자동 armed 금지

## 기록 규칙
항상 아래 순서가 복원 가능해야 한다.
1. host intent
2. board accept/reject
3. actual CAN TX raw
4. ack/event

## 하드웨어 연동
- `ESTOP_IN_N`은 isolated input으로 받고, firmware state와 별개로 CAN control TX gate에 반영한다.
- `CAN_TX_ENABLE`은 power-on default disabled다.
- `SAFETY_WD_TOGGLE`이 멈추면 외부 safety watchdog은 control TX gate를 disabled로 둔다.
- `FIELD_PWR_OK` false는 `BOARD_HEALTH`와 `BOARD_EVENT`로 남긴다.
- hard safety gate 상세 핀맵은 `board/docs/HARDWARE_FINAL_CONCEPT_KO.md`를 따른다.
