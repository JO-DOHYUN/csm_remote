# CONTROL_EVIDENCE_CONTRACT_KO

## 판정 기준
제어 성공 판정은 단계별로 분리한다.

1. Qt requested: Qt가 host command frame을 만들었다.
2. Serial written: PC가 USB serial write를 완료했다.
3. Board accepted/rejected: CSM이 `CONTROL_ACK`를 발행했다.
4. Actual CAN sent: CSM이 실제 CAN write 성공 후 `CAN_TX_RAW`를 발행했다.
5. External feedback: 외부 장치가 반응한 `CAN_RX_RAW` 또는 VCU feedback이 들어왔다.

## CONTROL_ACK
`CONTROL_ACK`는 보드가 host request를 접수하거나 거절했다는 evidence다. 이
record만으로 CAN analyzer에 실제 frame이 나갔다고 판정하지 않는다.

현재 v1 payload size와 numeric status는 Qt 호환을 위해 유지한다. status/reason
확장은 shared contract에 reserved/next-phase로 먼저 문서화한 뒤 구현한다.

## CAN_TX_RAW
`CAN_TX_RAW`는 CSM이 실제 CAN controller write 성공을 확인한 뒤 발행하는 audit
record다. Qt/VMS에서 actual sent 표시는 이 record를 기준으로 한다.

## Reject/Failure Visibility
reject, queue full, safety block, CAN not ready, write fail, parser error는
`CONTROL_ACK`, `BOARD_EVENT`, `BOARD_HEALTH` 중 최소 하나로 관측 가능해야 한다.

