# CSM Remote 운영 시나리오

## 1. 부팅

M7은 safety inhibit 상태로 시작한다. M4 RC sample, autonomy 상태, hardware gate, CAN backend가 각각 유효해도 명시된 authority 조건 전에는 local CAN TX를 허용하지 않는다. VSM 미연결은 RC 안전 판단을 바꾸지 않는다.

## 2. RC 운용과 동시 관측

RC input은 M4에서 최신 sample로 M7에 전달된다. M7은 authority와 safety를 판정한다. 동시에 CAN evidence는 canonical publisher를 거쳐 Windows USB와 Android Wi-Fi에 전달된다. 두 sink의 처리 지연은 RC tick이나 CAN ingest를 막지 않는다.

## 3. 고부하 dual CAN + USB + Wi-Fi

CAN ingest와 RC가 최고 우선순위를 가진다. publisher admission과 각 sink queue는 정적 한계를 가진다. 한계를 넘으면 정의된 우선순위로 drop/suppression하고 counters와 identity gap을 노출한다. 메모리를 무한 확장하거나 전체 pipeline을 멈추지 않는다.

## 4. Wi-Fi client 정지

Android가 읽지 않거나 무선 품질이 저하되면 Wi-Fi sink queue만 포화된다. Wi-Fi sink는 자체 drop/timeout/close reason을 기록하고 공유 frame 참조를 제한 시간 내 해제한다. USB와 RC는 계속 동작한다.

## 5. Wi-Fi 재접속

연결 종료 후 board backlog를 재생하지 않는다. 새 연결은 새 sink epoch로 시작하고 현재 live stream을 받는다. Android는 epoch 변화와 identity gap을 손실로 기록한다.

## 6. USB 단절

USB CDC가 닫히거나 host가 읽지 않아도 Wi-Fi와 RC는 계속 동작한다. USB sink만 bounded drop과 epoch/counter를 갱신한다. USB 재연결 때문에 CAN 수신 queue나 Wi-Fi queue를 초기화하지 않는다.

## 7. CSM reset

boot/session identity가 바뀐다. 두 VSM은 이전 session과 새 session을 이어 붙이지 않고 reset evidence로 분리한다. 단순 `seq` wrap과 reboot를 혼동하지 않아야 한다.

## 8. Service/HIL

명시된 Full Instrumented artifact와 안전한 bench에서만 host 제어를 허용한다. source context, authority decision, `CONTROL_ACK`, matching `CAN_TX_RAW`를 함께 기록한다. production observer artifact로 같은 시험을 수행하지 않는다.
