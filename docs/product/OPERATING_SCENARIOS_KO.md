# CSM Remote 운영 시나리오

## 1. 부팅

M7 application은 위험한 driver보다 먼저 reset evidence와 retained recovery를
복구하고 새 boot marker를 commit한다. safety inhibit 상태에서 시작하며 hard
safety 허용, upstream autonomy의 명시적 `InactiveConfirmed`, RC neutral handoff,
실제 vehicle mapping, hardware gate, CAN backend가 모두 유효하기 전에는 local
CAN TX를 허용하지 않는다. VSM 미연결은 RC 안전 판단을 바꾸지 않는다.

## 2. RC 운용과 동시 관측

RC input은 M4에서 최신 sample로 M7에 전달된다. M7은 hard safety, upstream
autonomy release, RC, service host 순서로 authority를 판정한다. 동시에 CAN
evidence는 canonical publisher를 거쳐 Windows USB와 Android Wi-Fi에 전달된다.
두 sink의 application queue와 lock은 독립적이다. 다만 Wi-Fi와 main은 같은 M7과
전원 경로를 공유하므로 vendor driver·radio·power fault의 물리 격리는 별도 gate다.

## 3. 고부하 dual CAN + USB + Wi-Fi

CAN ingest와 RC가 최고 우선순위를 가진다. publisher admission과 각 sink queue는 정적 한계를 가진다. 한계를 넘으면 정의된 우선순위로 drop/suppression하고 counters와 identity gap을 노출한다. 메모리를 무한 확장하거나 전체 pipeline을 멈추지 않는다.

## 4. Wi-Fi client 정지

Android가 읽지 않거나 무선 품질이 저하되면 Wi-Fi sink queue만 포화된다. Wi-Fi sink는 자체 drop/timeout/close reason을 기록하고 공유 frame 참조를 제한 시간 내 해제한다. USB와 RC는 계속 동작한다.

## 5. Wi-Fi 재접속

연결 종료 후 board backlog를 재생하지 않는다. 새 연결은 새 sink epoch로 시작하고 현재 live stream을 받는다. Android는 epoch 변화와 identity gap을 손실로 기록한다.

## 6. USB 단절

USB CDC가 닫히거나 host가 읽지 않아도 Wi-Fi와 RC는 계속 동작한다. USB sink만 bounded drop과 epoch/counter를 갱신한다. USB 재연결 때문에 CAN 수신 queue나 Wi-Fi queue를 초기화하지 않는다.

## 7. CSM reset

boot/session identity가 바뀐다. 두 VSM은 이전 session과 새 session을 이어 붙이지
않고 reset evidence로 분리한다. 단순 `seq` wrap과 reboot를 혼동하지 않는다.
CSM은 이전 boot의 stable 여부, 마지막 progress, early-reset 누계를 backup SRAM에서
복구한다. 같은 source/experiment selector가 30초 전에 연속 2회 종료되면 다음 boot의 Wi-Fi를
quarantine해 USB와 retained evidence를 우선 살린다.

application이 보는 RCC raw flag는 bootloader가 이미 clear했을 수 있다. 따라서
raw unknown, reset 간격, LED만으로 watchdog이나 power fault를 확정하지 않는다.
정확한 reset source는 bootloader early latch 또는 외부 power/reset evidence가
필요하다.

## 8. Service/HIL

명시된 Full Instrumented artifact와 안전한 bench에서만 host 제어를 허용한다.
source context, authority decision, `CONTROL_ACK`, `CAN_TX_RAW`, 외부 CAN analyzer를
함께 기록한다. 현재 built-in driver의 enqueue 수락 직후 생성된 `CAN_TX_RAW`만으로
physical bus TX 성공을 주장하지 않는다. production observer artifact로 같은
시험을 수행하지 않는다.

## 9. Reset 원인 분리 시험

reset experiment artifact는 application-data CAN TX를 compile-time 차단하고 USB typed
evidence를 유지한다. REF는 watchdog+full Wi-Fi, A는 watchdog+Wi-Fi Off, B는
watchdog Off+full Wi-Fi, C는 watchdog+AP-only다. profile 사이에는 selector만
바꾸고 source set, RC/CAN/USB 진단 경계를 같게 유지한다.

각 run은 source/build identity, selector, boot sequence/session, 30초 stable marker,
last progress, main gap, Wi-Fi phase, retained integrity와 CAN TX 0을 판정한다.
한 profile의 180초 통과만으로 release를 승인하지 않고 반복 재현, fault injection,
동시 부하와 장시간 soak로 이어간다.

## 10. 제품 제어 승인 전 운용

Production Remote profile의 vehicle mapper 기본값은 `None`이고 local CAN TX
capability도 Off다. `0x007` MDPS mapping은 명시적 bench artifact에만 허용한다.
upstream autonomy runtime wiring, 실제 차량 mapping, D1 hardware gate 의미,
completion-correlated `CAN_TX_RAW`, 외부 analyzer HIL 전에는 RC data를 관측할 수
있어도 차량 제어 제품으로 운용하지 않는다.
