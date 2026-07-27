# CSM 검증 정책

## 2026-07-27 제품 gate

하드웨어 시험 전에 `firmware/csm/tools/product_envelope.py`를 실행한다.
그 다음 하나의 결과 table에서 idle, nominal, 2,000 fps, 선택적 4,000
fps, blocked client, reconnect, RC+CAN+USB+Wi-Fi 동시 부하를 비교한다.

필수 열은 source frame/byte, canonical accepted B/s, socket B/s,
request/positive-write 평균, queue high-water, Reserved/Full, close
reason/epoch, CRC/typed/segment/capture gap, source drop, main-loop 최대 gap,
worker stack 최솟값이다. 처리량만 맞아서는 통과가 아니다. 요구 integrity
counter가 모두 0이고 queue conservation과 firmware identity가 확인되어야
한다. 4,000 fps는 기본 성공 주장이 아니라 hardware/transport capability
gate다.

2026-07-27 최종 nonblocking 후보는 fresh-boot idle gate에서 실패했다.
WHD/lwIP socket이 2,954 byte 이후 무진행 상태가 되었고 5초 정책으로
격리됐다. 같은 창의 CAN/USB/core 무결성은 통과했다. 따라서 내장 Wi-Fi는
release blocker이며, idle이 통과하기 전 2,000/4,000 fps 성공을 주장하지
않는다. 상세 수치는 `history/evidence/2026-07-27_csm_product_hil.json`이
권위다.

## 증거 등급

### H0 Harness

- 권위 문서와 경로 검증
- legacy router/문서 부재
- `git diff --check`

### C1 Compile/Static

- remote phase guards
- passive M7 build
- M4 frontend proof build
- M4 Serial3 capture probe build
- protocol fixture/unit test

### I1 Transport Integration

- canonical publisher + USB sink byte parity
- queue capacity, priority, drop counter
- disconnect/reconnect epoch
- Wi-Fi 단일 client live stream

### I2 Simultaneous HIL

- 실제 R16SM + M4-M7 IPC
- dual CAN 고부하
- Windows USB VSM + Android Wi-Fi VSM 동시 관측
- blocked USB와 blocked Wi-Fi 각각의 격리
- RC latency, stale/failsafe, authority, CAN TX evidence
- 실제 사용 채널의 raw span, neutral/deadband, 정규화 방향과 CRSF link
  statistics/RSSI freshness
- CRSF heartbeat/flight-mode TX counter와 receiver-side telemetry 확인
- 승인된 실제 vehicle mapping의 각 ID/payload/period, FDCAN TX completion과 상관된
  `CAN_TX_RAW`, 외부 analyzer 수신·ACK의 일치
- transmitter off/on에서 즉시 neutral, 1 s release, 재연결 500 ms neutral handoff
- REF/A/B/C 반복 reset matrix, retained progress/integrity, bootloader 또는 외부
  reset-source evidence

### R1 Release

- 장시간 soak와 반복 power cycle
- packet loss/reorder/session 분석
- CPU, memory, queue high-water budget
- production tablet 및 Windows PC 조합
- 차량 안전 검토와 artifact identity

## 합격 원칙

- build 성공을 hardware 성공으로 주장하지 않는다.
- TCP 연결을 end-to-end 무결성으로 주장하지 않는다.
- 선언 부하는 timed window 전에 counter로 증명한다. Full TCP는 effective Full, quarantine off, 실제 client connect와 Wi-Fi sent 진행이 모두 필요하다.
- `wifi_connect=0` 또는 `wifi_sent delta=0`인 결과는 USB/idle evidence일 뿐 Wi-Fi·reconnect 합격으로 승격하지 않는다.
- generic PASS 대신 실행한 gate를 `IDLE_STABILITY`, `CONNECTED_TCP`, `RECONNECT_CHURN`, `I2_SIMULTANEOUS`로 함께 기록한다.
- total count만 보지 않고 source capture, canonical publish, sink commit, app admission/capture를 대조한다.
- 무결성 목표 수치는 실제 입력률·기기·지속시간·fault profile과 함께 기록한다.
- 현재 3400 frame/s 1시간 동시 운용은 목표이며 아직 검증 결과가 아니다.

## 변경별 최소 검증

- docs/harness: H0
- RC/authority/CAN code: 관련 guard + M4/M7 build + unit/fixture
- publisher/wire: H0 + C1 + USB byte parity
- Wi-Fi sink: C1 + I1 blocked-client/reconnect
- Wi-Fi queue/throughput: 선언 source byte율 × stall timeout으로 byte envelope를 정하고,
  정상 client timed window에서 disconnect/stall/overflow와 typed/segment/capture gap이
  모두 0이어야 한다. queue 증설만으로 평균 생산율 초과를 숨기지 않는다.
- product profile: C1 + I2
- release 주장: R1
