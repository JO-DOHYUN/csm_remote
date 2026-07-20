# CSM Uplink Transport 아키텍처

## 목표

USB CDC Windows VSM과 Wi-Fi TCP Android VSM이 동일한 typed evidence를 독립적으로 받되, 어느 sink의 장애도 RC·CAN ingest·publisher·다른 sink에 전파되지 않게 한다.

## 최종 모듈

```text
Record producers
  -> PriorityAdmission
  -> CanonicalPublisher
       - canonical order/identity owner
       - typed frame encode once
       - one immutable encoded frame view per publication
  -> UsbCdcSink fixed queue
  -> WifiTcpSink fixed queue
```

### PriorityAdmission

record priority, pool reserve, queue capacity, suppression 정책을 소유한다. CAN truth reserve와 diagnostic suppression을 분리한다.

### CanonicalPublisher

sink보다 앞에서 publish order와 identity를 한 번 배정하고 typed bytes를 한 번 생성한다. sink 연결 여부에 따라 record 내용이나 순서를 바꾸지 않는다.

### UsbCdcSink / WifiTcpSink

각 sink는 별도 queue, cursor, connection epoch, timeout, counter, high-water, first/last identity, close reason을 소유한다. transport API write는 자기 service budget 안에서만 수행한다.

연결 수락은 session publication gate보다 먼저 nonblocking poll한다. listener backlog에 TCP handshake가 존재하더라도 `hasConnectedSink()`만 먼저 검사해 loop를 종료하면 firmware가 client를 영원히 accept하지 못하므로, 매 loop에서 sink connection state를 먼저 진행한 뒤 session state를 판단한다.

## identity 계층

다음 값은 합치지 않는다.

- CAN capture identity: bus별 `capture_seq64`
- segment identity: `segment_seq64`
- canonical publish identity: boot/session + monotonic publish sequence
- sink delivery identity: sink epoch + first/last attempted/committed publish identity
- Android durable capture identity: 앱 admission/capture commit sequence

v1 header의 `seq u16`은 fanout 전 `CanonicalPublisher`가 배정하는 `publish_seq64`의 하위 16비트다. `STREAM_SESSION` record가 boot/session ID와 full `publish_seq64`를 boot, sink epoch 변경, 16비트 wrap마다 고정한다. Android 전용 envelope는 사용하지 않는다.

## bounded fanout 규칙

- producer와 publisher는 sink socket/CDC를 기다리지 않는다.
- sink queue는 정적 용량이며 overflow가 관측 가능하다.
- publisher는 한 번 encode한 immutable view를 각 sink의 정적 queue로 복사하고 즉시 반환한다. sink는 publisher buffer를 보유하지 않는다.
- 높은 우선순위 record를 보호하되 손실을 숨기지 않는다.
- network reconnect는 새 epoch이며 board backlog replay를 수행하지 않는다.
- 초기 Wi-Fi 제품은 Android observer 1대만 허용한다.
- TCP는 전송 순서와 신뢰성을 제공하지만 application loss/session 의미를 대신하지 않는다.
- Wi-Fi write가 일시적으로 0을 반환해도 즉시 장애로 단정하지 않는다. 현재 stalled-client close 기준은 5 s이며 queue와 write는 계속 bounded/nonblocking이다.
- Wi-Fi observer와 Service/HIL profile은 48-record fixed sink queue를 사용하고 그중 4개를 critical health/control evidence에 예약한다. CAN truth는 레코드 2개 또는 최대 75 ms까지 모아 한 socket write로 전송하며, critical health/control evidence는 즉시 flush한다.
- 48-record 수치는 20 Hz CAN1 bench에서 약 1 s Wi-Fi write 정지와 32-record queue overflow 4건이 실측되어 transient를 흡수하도록 정한 현재 기준이다. 정적 `CAPABILITY`는 session 시작과 요청 응답 외에 10 s 주기로 복구 광고하며, 고부하 HIL에서 queue/throughput 기준을 다시 확정한다.
- Wi-Fi backpressure 전환마다 같은 혼잡 sink에 `BOARD_EVENT`를 재주입하지 않는다. queue high-water, overflow, stall/epoch counter를 `BOARD_HEALTH`에서 집계해 피드백 데이터 스톰을 방지한다.

## 실패 격리

| 실패 | 영향 범위 | 필수 evidence |
|---|---|---|
| USB host 미수신 | USB sink만 | queue high-water, drop, epoch/close |
| Wi-Fi client 미수신 | Wi-Fi sink만 | timeout, drop, close reason |
| Wi-Fi disconnect | Wi-Fi sink만 | epoch change, first/last identity |
| publisher pool 고갈 | uplink admission | record priority별 drop, pool high-water |
| CAN ingest overflow | CAN evidence source | bus별 capture/drop counter |

## 구현 상태와 다음 gate

구현 완료:

- `CanonicalPublisher`, `RecordAdmission`, fixed `UsbCdcSink`, fixed `WifiTcpSink`
- CSM AP direct, TCP `192.168.4.1:3333`, observer client 1개
- sink epoch, overflow, high-water, frame progress, stalled-client close 계측
- Wi-Fi record batching, critical 즉시 flush, critical queue reserve
- Wi-Fi 비활성 profile에서 Wi-Fi library와 queue를 링크하지 않는 build 분리

실기 완료:

- Portenta Wi-Fi observer firmware upload
- AP 생성, Android TCP connect, `STREAM_SESSION`/`BOARD_HEALTH` 수신
- 60 s status-stream 동안 sink disconnect/stall/overflow와 앱 integrity 오류 0
- Service/HIL profile에서 Kvaser CAN1 `0x50` 20 Hz를 30 s 계측해 CSM health-window `580/580`, CAN/FIFO/Wi-Fi drop 0, typed/segment/capture gap 0

남은 gate:

1. controlled dual-CAN source count와 Android evidence를 대조한다.
2. blocked-client와 reconnect fault injection을 수행한다.
3. RC + dual CAN + USB + Wi-Fi 동시 HIL과 장시간 soak를 통과한다.
