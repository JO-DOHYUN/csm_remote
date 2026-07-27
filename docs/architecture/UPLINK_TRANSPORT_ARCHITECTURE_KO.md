# CSM Uplink Transport 아키텍처

## 2026-07-27 확정 구조

현재 권위 구조는 `docs/quality/CSM_PRODUCT_ENVELOPE_KO.md`와 D-026을
따른다. CanonicalPublisher는 record identity와 bytes를 한 번만 만들고,
USB와 Wi-Fi가 독립 bounded sink로 소비한다. Wi-Fi는 외부에는 AP만
제공하되 Portenta WHD 내부는 실측 검증된 `ap_sta_concur=true` 호환
역할로 시작하며, direct WHD 단일 socket worker와 외부 DTCM byte queue를
사용한다. admission priority와 delivery latency는 별도 계약이며, 96%
high-water는 Reserved/Full 전에 한 번의 새 epoch를 요청한다. 5초 socket
무진행은 별도 close 원인이다.

CAN_RX_SEGMENT schema 2는 40-byte header + 20-byte entry로 23개 frame을
lossless하게 담는다. 두 CAN source queue는 최소 capture sequence 순서로
merge한다. 아래의 48 KiB/512 descriptor, critical 즉시 flush, high-water는
close 조건이 아니라는 설명은 과거 설계이며 이 절과 충돌할 때 폐기한다.

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
  -> WifiTcpSink fixed byte pool + frame descriptor queue
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
- Wi-Fi write가 일시적으로 0을 반환하거나 queue가 high-water를 통과해도 client 장애로 단정하지 않는다. high-water는 즉시 drain을 유도하는 scheduling hint일 뿐 close 조건이 아니다. socket이 실제로 2.5 s 연속 무진행일 때만 `TransmitNoProgress`로 닫는다. 그보다 느리더라도 계속 전진하는 client가 admission의 Reserved/Full에 닿으면 최초 손실에서 producer가 기다리지 않고 한 번만 `QueuePressure` epoch 종료를 요청한다.
- Wi-Fi observer와 Service/HIL profile은 48 KiB byte pool과 512개 frame descriptor를 사용한다. 2 KiB byte와 4개 descriptor는 critical health/control evidence에 예약한다. descriptor는 16 B committed-frame cursor이며 payload는 byte pool에만 한 번 저장된다. 16-bit monotonic byte-end 차분은 48 KiB full과 32-bit counter wrap에서도 정확하고, `descriptor_tail` release가 유일한 record commit이므로 worker는 미완료 payload를 읽지 않는다. 이 구조는 `record 수 × 최대 frame 크기` 메모리 낭비 없이 byte와 record 수를 각각 bounded한다.
- queue envelope는 현재 200 Hz `CAN_TX_RAW`를 포함한 실제 record 수/byte 부하로 정했다. queue 수위만으로는 닫지 않는다. Reserved/Full은 이미 해당 epoch의 무결성이 깨진 admission loss이므로 producer가 atomic one-shot latch로 worker에 close/abort를 요청하고 즉시 복귀한다. worker의 epoch 증가가 main에 관측될 때까지 재-admission을 막아 이전 epoch bytes가 새 연결로 넘어가지 않게 한다.
- Remote Product의 CAN truth는 최대 20 ms 동안 최대 15 frame을 segment로 묶고, Wi-Fi worker는 최대 1024 B를 한 nonblocking send로 전달한다. critical record는 batch 대기 없이 전송 대상이 된다.
- Remote Product의 정적 `CAPABILITY`는 session 시작·재연결 시 광고한다. periodic 광고는 reset 실험의 변수를 줄이기 위해 현재 Off지만, 과거 reset을 해당 광고나 정확히 3초 watchdog으로 확정하지 않는다.
- 기본 Remote Product와 reset experiment는 외부 MCP2515를 compile-out하고 J4 built-in CAN을 관측한다. 명시적 MDPS bench profile만 MCP2515를 normal-mode RX/ACK로 열며 MCP/host control TX는 계속 금지한다. MCP2515를 RP2040 feeder로 교체할지는 별도 hardware/product gate이며 아직 확정하지 않는다. 외부 frontend를 바꾸더라도 authority와 canonical publish identity는 M7이 소유한다.
- Wi-Fi backpressure 전환마다 같은 혼잡 sink에 `BOARD_EVENT`를 재주입하지 않는다. queue high-water, overflow, stall/epoch counter를 `BOARD_HEALTH`에서 집계해 피드백 데이터 스톰을 방지한다.

## 실패 격리

| 실패 | 영향 범위 | 필수 evidence |
|---|---|---|
| USB host 미수신 | USB sink만 | queue high-water, drop, epoch/close |
| Wi-Fi client 미수신 | Wi-Fi sink만 | timeout, drop, close reason |
| Wi-Fi disconnect | Wi-Fi sink만 | epoch change, first/last identity |
| Wi-Fi vendor call 장기 block | 동일 M7 system 영향 가능 | call phase/sequence/start, worker heartbeat, retained progress |
| radio/전원/reset path 장애 | board 전체 영향 가능 | boot session/sequence, retained recovery, 외부 power/reset evidence |
| publisher pool 고갈 | uplink admission | record priority별 drop, pool high-water |
| CAN ingest overflow | CAN evidence source | bus별 capture/drop counter |

## 구현 상태와 다음 gate

### 2026-07-22 Wi-Fi 실행 경계 보강

```text
CanonicalPublisher
  -> WifiTcpSink facade (try-offer와 cached 상태만 소유)
  -> bounded TX/RX mailbox (epoch/generation 포함)
  -> WifiSocketWorker (AP/accept/send/recv/close와 socket lifetime 단독 소유)
```

- 메인 루프와 publisher는 socket API를 호출하거나 기다리지 않는다.
- Mbed `TCPSocket::accept()`가 반환한 factory socket은 `close()`가 객체까지 해제한다. worker는 close 뒤 포인터를 참조하거나 별도 `delete`하지 않는다.
- 워커는 static 16 KiB stack의 단일 수명 thread이며 재생성하지 않는다.
  실제 free/max-used stack은 debug record에서 1초 주기로 계측한다.
- 현재 제품 Wi-Fi worker는 1 ms 주기로 실행하고 한 pump에서 최대 4회/4096 B/1000 us까지
  bounded nonblocking send를 수행하는 Normal priority이며, 메인은 끝에서 1 ms slice를 양보한다.
  이는 application thread 간 실행 계약이지,
  같은 M7의 vendor driver/kernel/IRQ stall이나 radio·전원 장애로부터 물리
  격리한다는 뜻은 아니다. 연결 전 accept poll 요청은 25 ms다.
- active client가 존재하는 동안 listener `accept()`를 다시 호출하지 않는다.
  제품은 observer 1대만 허용하며, 불필요한 extra-client poll이 active TX/RX
  진행과 250 ms call-isolation 판정을 교란하지 않게 한다.
- TX queue lock은 socket 호출 전에 해제한다. abort generation이 바뀐
  늦은 send 결과는 새 epoch queue에 적용하지 않는다.
- RX mailbox overflow는 부분 downlink를 숨기지 않고 Wi-Fi epoch를 닫는다.
- `Disabled`, `AccessPointOnly`, `FullTcp` mode를 명시적으로 분리한다.
  Disabled는 worker와 Wi-Fi API를 시작하지 않고, AP-only는 configure/AP까지만
  실행하며 TCP server/socket을 만들지 않는다.
- startup attempt는 기본 1회로 제한해 무한 retry를 제거했다. 그러나 한 번의
  `beginAP()` vendor call 자체가 반드시 제한 시간 안에 반환한다는 뜻은 아니다.
- 250 ms 이상 진행 중인 Wi-Fi call은 phase/sequence/start와 worker heartbeat로
  관측한다. application lock/mailbox 관점에서는 Wi-Fi sink만 닫거나 격리할 수
  있지만, stuck vendor call이 board 전체 진행을 막지 않는다고 보장하지 않는다.
- runtime diagnostic은 worker 내부에서 record를 publish하지 않는다.
  워커는 호출 전·후의 작은 snapshot만 갱신하고, 메인이 100 ms 주기로
  이를 canonical debug record와 backup-SRAM retained slot에 기록한다.
- 같은 source/experiment selector의 30초 이전 종료가 연속 2회 복구되면 다음 boot는 Wi-Fi를
  시작하지 않고 USB와 retained evidence를 살리는 quarantine으로 진입한다.
- deterministic mailbox/fault contract와 socket ownership guard가 HIL보다
  먼저 통과해야 한다.

### 2026-07-26 raw AP/TCP 병목 격리

- 제품 publisher·queue·CAN·RC·feeder를 제외한 영구 raw 진단 profile 두 개를 추가했다.
  둘 다 같은 Portenta SoftAP와 Mbed 6.17/lwIP 기본 profile을 사용한다.
- PC 30초 무결성 수신에서 blocking은 2,103,264 B, 평균 69,413 B/s였고,
  nonblocking+`sigio`는 1,970,872 B, 평균 65,083 B/s였다. 두 경로 모두
  pattern mismatch와 unexpected close는 0이지만 1초 수신량 0인 구간이 있었다.
- blocking `send()`는 최대 1,046,520 us 동안 머물렀고, `sigio` 경로는
  최대 4,297 ms 무진행을 관측했다. 따라서 기존 약 13 kB/s 제품 결과의 원인을
  제품 queue 하나로 한정할 수 없으며, 현재 Mbed/lwIP/SoftAP 경로 자체에도
  처리량과 지연 변동 문제가 있다.
- 이 결과는 무선 하드웨어 최대치 판정이 아니다. 다음 격리 gate는 동일 raw
  시험의 tuned Mbed profile 비교이며, 이후 STA/lwiperf로 SoftAP·TCPSocket
  wrapper·하위 driver 경계를 추가 분리한다.
- 문서 초안의 production-candidate 값(MSS 1460, send buffer 4 MSS,
  window 6 MSS, mem 32768, TCP/IP stack 4096)을 실제 Mbed 6.17로 재빌드했으나
  raw 펌웨어 링크에서 `RAM_D2`가 16,403 B 초과했다. 이 profile은 폐기했으며
  제품에 적용하지 않는다. 다음 profile은 실제 linker section별 잔여량 안에서
  산정하고 raw A/B를 통과해야 한다.

구현 완료:

- `CanonicalPublisher`, `RecordAdmission`, fixed `UsbCdcSink`, fixed `WifiTcpSink`
- CSM AP direct, TCP `192.168.4.1:3333`, observer client 1개
- sink epoch, overflow, high-water, frame progress, stalled-client close 계측
- Wi-Fi record batching, critical 즉시 flush, critical queue reserve
- 20 ms CAN segment aggregation, 1024 B TX chunk, 48 KiB/512 descriptor sink envelope
- Wi-Fi 비활성 profile에서 Wi-Fi library와 queue를 링크하지 않는 build 분리
- reset experiment의 Wi-Fi Off/AP-only/Full runtime mode와 startup 1회 제한

실기 완료:

- Portenta Wi-Fi observer firmware upload
- AP 생성, Android TCP connect, `STREAM_SESSION`/`BOARD_HEALTH` 수신
- 60 s status-stream 동안 sink disconnect/stall/overflow와 앱 integrity 오류 0
- Service/HIL profile에서 Kvaser CAN1 `0x50` 20 Hz를 30 s 계측해 CSM health-window `580/580`, CAN/FIFO/Wi-Fi drop 0, typed/segment/capture gap 0
- MDPS bench profile과 PCAN/J4 약 130 frame/s 입력에서 PC TCP monitor 60 s 계측:
  단일 boot/session/epoch, `wifi_disconnect/stall/socket_error/overflow=0`,
  `CAN drop/FIFO=0`, CRC와 typed/segment/capture gap 0, queue high-water 24,831 B

남은 gate:

1. 180초 1차 통과한 REF/A/B/C를 반복·장시간 창과 fault injection으로 확장한다.
2. bootloader reset latch 또는 외부 power/reset evidence로 정확한 reset source를
   보존한다.
3. blocked-client와 reconnect fault injection을 수행한다.
4. controlled dual-CAN source count와 Android evidence를 대조한다.
5. RC + dual CAN + USB + Wi-Fi 동시 HIL과 장시간 soak를 통과한다.

상세 retained 배치와 profile 판정표는
`DEBUG_AND_RECOVERY_ARCHITECTURE_KO.md`를 따른다.
