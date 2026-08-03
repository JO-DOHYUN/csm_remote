# BRIEF

Updated: 2026-08-03

## 2026-08-03 Wi-Fi transient-envelope candidate

- D-031 supersedes D-030 only for live FIFO dimensions and close policy. The
  live-only/no APP ACK/no network replay product identity is unchanged.
- Product Wi-Fi now uses `256 descriptors / 49,152 encoded bytes`, with
  `4 descriptors / 2,112 bytes` reserved for critical evidence. The generated
  135,000 B/s, 686 records/s envelope proves 250 ms transient coverage in both
  dimensions while leaving a 65,536 B linker-enforced DTCM reserve.
- USB now uses the same byte-ring/descriptor primitive with `192 descriptors /
  40,960 encoded bytes`. At the 135,000 B/s, 686 records/s envelope its
  250 ms requirement is 34,273 B/173 records, so admission is bounded by a
  declared transient rather than the former eight max-sized record slots.
- `32,768 B 또는 192 records` high-water and `8,192 B/64 records` low-water
  form diagnostic hysteresis. High-water and a single `WOULD_BLOCK` no longer
  close the epoch. Only an actual reserve/full admission miss closes with the
  legacy numeric reason 6 and exact loss range; five seconds without positive
  socket progress closes as reason 4.
- The product Service/HIL M7 build passed: source manifest
  `a00811b5bf2c83c61814d7d0312aecae8d03cfc012b24dc3112a4b9401cd8f2a`,
  firmware.bin SHA-256
  `0C5AC5EB072B61299850D00E420A38857602A871ABF8FFF6FF5536B26526FE8E`,
  RAM `211,448/523,624 B (40.4%)`, firmware image `368,320/786,432 B`
  (46.8%; PlatformIO section meter `367,192 B`), D1
  heap span `311,816 B`, Wi-Fi DTCM storage `53,248 B`, calculated DTCM
  remainder `77,152 B`.
- Envelope/architecture guards, uplink/Wi-Fi host contracts and compact
  `CAN_RX_SEGMENT` schema-2 evidence decoder tests pass. PC USB/Wi-Fi and
  dual-CAN HIL tools now count compact entries rather than silently ignoring
  them.
- This exact artifact was uploaded by DFU to `0x08040000`. The 60 s PC
  USB+Wi-Fi+dual-CAN gate passed: PCAN 120,000 and Kvaser 120,000 were exact on
  both sinks; CRC/typed/segment/capture gaps, CAN/FIFO/pool/USB/Wi-Fi loss,
  close and conservation residual were all 0. Active Wi-Fi accepted/drained
  100,190/100,207 B/s and ended with 981 B net queue reduction. The
  boot-cumulative USB high-water was 519 B with overflow 0. Minimum-timestamp
  segment bases preserved capture order while packing 241,627 frames into
  10,673 segments (22.64 average). The proof artifact is
  `C:\WORKS\VS\vsm_android_app\artifacts\hil_dual_usb_highload_2000_2000_20260803_193748\result.json`.
- Android+Capture, RC+CAN-TX simultaneous I2, separate 120/135 kB/s capacity,
  reconnect/fault injection and soak remain OPEN.

## 2026-07-31 feeder Service/HIL tablet gate

- Added `portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi`. It keeps
  feeder CAN0 ingress, M4 RC, J4 CAN1 and pinned product Mbed while advertising
  full-instrumented profile 2 and opening only the bounded Wi-Fi Service/HIL
  host path. The observer artifact was not changed or built.
- Control execution guards and the M7 build passed at 171,104 B RAM and
  370,224 B flash. COM7 DFU upload completed successfully.
- TOP10 Android 9 tablet reconnected to `VSM-CSM-DEV`, received both CAN lanes,
  accepted ARM through `CONTROL_ACK`, entered `ArmedIdle`, and disarmed on
  background. Android local Capture remained `CORRUPT`; that independent app
  storage issue is still open and does not qualify the full release gate.

## 2026-07-30 live-first observer architecture decision

- D-030 supersedes D-028. Current approved Wi-Fi direction is a
  `128-record / 8,192-byte` nonblocking live FIFO: positive socket send
  releases bytes immediately; there is no APP ACK, retained network journal,
  reclaim cursor, disconnect rewind, or backlog replay.
- Queue overflow or socket stall closes and flushes only the Wi-Fi epoch. The
  next client begins with a fresh current `STREAM_SESSION` and receives current
  Live only. Exact drop/flush/epoch/close evidence remains visible; RC, CAN,
  canonical publication, and USB continue independently.
- Android Capture is app-local storage. Its open/write/fsync/storage failure
  cannot fence CSM admission, close TCP, or stop Live.
- Record `21 APP_RX_COMMIT_ACK` is reserved legacy decode-ignore. Record `22
  LINK_RELIABILITY_DIAGNOSTIC` schema 1 is legacy decode-only and is not a
  current product publication.
- Startup order is safe/inhibit → reset evidence → M4 RC/M7 authority-safety →
  CAN → USB → Wi-Fi. Internal Portenta Wi-Fi remains an architecture candidate
  because it shares the M7/kernel/SDIO/power common-cause boundary.
- The live-first implementation, host contracts, product build, COM7 upload,
  and a 15-second USB+PC Wi-Fi live gate passed on 2026-07-30. That gate saw
  one boot session, CRC/typed/segment/capture gaps 0, Wi-Fi overflow/stall/socket
  error 0, and byte/record conservation residual 0. The artifact remains
  `RELEASE BLOCKED` until actual Android AP operation, simultaneous RC/dual-CAN
  load, fault injection, and soak gates pass. Older dated sections below are
  historical evidence, not current release claims.
- The cross-project D-027 anchor-acceptance closure is implemented after that
  uploaded baseline. `STREAM_SESSION` remains one canonical publication to all
  connected sinks, while its requested sink mask is now enforced as a required
  acceptance mask. A missed requested Wi-Fi anchor isolates only that Wi-Fi
  epoch before normal records. Uplink/isolation host contracts, the Wi-Fi
  architecture guard and the feeder product M7 build pass at 170,880 B RAM and
  367,544 B flash. This newer artifact has not been uploaded or Android-qualified.

## 2026-07-28 retained Wi-Fi product candidate — superseded by D-030

The following records the rejected D-028 candidate and is not an active product
contract.

- The product Wi-Fi sink is now a nonblocking retained journal rather than a
  send-and-discard queue. `CanonicalPublisher` still encodes once; USB consumes
  independently, while Wi-Fi keeps 1,024 descriptors plus 65,520 encoded bytes
  until Android confirms a durable contiguous commit.
- Downlink record `21 APP_RX_COMMIT_ACK` carries
  `{boot_session_id u64, last_contiguous_publish_seq u64}`. A positive socket
  send advances only the send cursor. Only a valid application ACK reclaims
  storage; disconnect rewinds every unacknowledged record for replay.
- Uplink record `22 LINK_RELIABILITY_DIAGNOSTIC` exposes offered, admitted,
  socket-sent, ACK-reclaimed, retained/unsent/high-water, first-not-admitted,
  ACK reject, rewind, epoch, and close evidence. The first admission failure
  latches integrity loss, emits `BOARD_EVENT 52`, and isolates the epoch; the
  journal never overwrites old truth.
- The reproducible product Mbed archive is pinned to ArduinoCore-mbed
  `6816d442...` and Mbed OS `17dc3dc2...`, SHA-256
  `032494298FC6CAFAAD23277B8CBEB01F1BA75CA7F72CCD90383A850EE561FD70`.
  lwIP uses MSS 1460, send buffer 11,680 B, window 5,840 B, 40 segments,
  40,960 B heap, and a 4,096 B TCP/IP stack. WHD TX uses `PBUF_RAM`.
- The lwIP heap is link-checked in the dedicated D3 range
  `0x38000400..0x3800A7FF`; M7 installs a non-cacheable/shareable MPU override
  before Wi-Fi starts. Product-only linker checks do not affect legacy/bench
  builds.
- Calculated 2,000 fps load is 57,735 B/s. A 1.02 s outage requires 58,890 B,
  within the 63,408 B normal journal envelope. The 4,000 fps row remains a
  transport gate at 102,172 B/s.
- Offline closure passed: product M7 and legacy MCP builds, M4 remote frontend,
  RP2040 feeder, all 11 native contracts, architecture/control guards, and the
  product envelope. Current product M7 is 169,632 B D1 RAM and 364,856 B flash;
  Wi-Fi journal storage is 90,096 B DTCM. Hardware upload and AP/Android/HIL
  gates were intentionally not run while the owner disconnected components.

## 2026-07-27 CSM data-plane correction and final gate

- Re-analysis of the retained 128-record run corrected the earlier
  interpretation: during the first 20.732 s, accepted and socket rates differed
  by only 55 B/s and the queue repeatedly drained. The failure began with a
  distinct ~2.073 s lower-path zero-progress interval; the small 512-record
  queue then filled to 27,472 B and requested QueuePressure. The whole-window
  946.7 B/s "steady deficit" was an averaging artifact after the epoch close.
- D-022/D-024 queue conclusions were superseded by D-025/D-026. The then
  candidate boundary used direct WHD AP service with the Portenta-validated
  `ap_sta_concur=true` compatibility role, checked raw `TCPSocket` ownership,
  byte-based 1,024 B batching, independent latency classes, one pre-full close,
  and a 1,280-record/65,520-byte queue sized for 100 kbit/s x 5 s.
- `CAN_RX_SEGMENT` schema 2 packs 23 lossless delta entries in a 511-byte typed
  frame. At 2,000 fps it reduces CAN RX wire load from 65,762 to 44,437 B/s.
  CSM, Android, Windows, and PC decoders retain legacy support and reject
  unknown layouts visibly.
- The Wi-Fi raw arena moved to guarded M7 DTCM (86,000/130,408 B). M4 D2 and M7
  network D2 have paired non-overlapping linker ownership. Per-bus M7 CAN queues
  are reduced from 4,096 to 512 entries, saving 229,376 B while retaining
  256 ms coverage at 2,000 fps per bus.
- The authoritative calculation and release table are in
  `docs/quality/CSM_PRODUCT_ENVELOPE_KO.md` and
  `firmware/csm/tools/product_envelope.py`.
- Final uploaded M7 source identity is
  `341f948e99256a0cb07fd6883f64575c9a76b4038c11bb238ba1350fb14eec2e`.
  A fresh-boot 15.059 s idle gate failed: the PC received 2,954 bytes, then
  WHD/lwIP returned 469 consecutive `WOULD_BLOCK` results and the isolated
  worker closed the epoch after 5 seconds. In the same boot, CAN received 945
  frames with zero CAN/FIFO/USB overflow and zero typed/segment/capture gap;
  main-loop maximum gap was 3,311 us. The board/core isolation passed, but the
  internal Wi-Fi product transport is not release-qualified. The 2,000 fps
  Wi-Fi row was not repeated because its idle prerequisite failed.

## 2026-07-27 event-driven Wi-Fi data plane and bounded evidence

- The Wi-Fi socket worker no longer polls every 1 ms. Producer transitions,
  critical/control requests, and raw-socket `sigio` wake one RTOS EventFlags
  owner; a 10 ms connected fallback covers coalesced/lost notification, and
  positive bounded progress self-wakes until the queue is drained.
- Main/RC/CAN remain nonblocking producers. Socket calls, close, abort, and
  queue consumption remain worker-owned; state publication is coalesced to
  100 ms except connection transitions.
- Canonical record `20 TRANSPORT_DIAGNOSTIC` publishes one 128-byte snapshot at
  1 Hz with offer/accept/queue/socket/wake/epoch/sequence boundaries. It uses
  the bounded 128-byte diagnostic lane and is not pre-suppressed by sink
  backlog.
- Host contract, architecture guard, and the product M7 build pass. Product
  memory is 448,632/523,624 B RAM (85.7%) and 374,544/786,432 B flash (47.6%);
  upload to COM7 passed.
- The final 30 s USB+PC-Wi-Fi gate kept one boot session with zero CRC,
  typed/segment/capture gaps, CAN/USB loss, pool failure, or diagnostic
  suppression. It failed at the Wi-Fi sink: accepted 8,943.4 B/s versus socket
  7,996.7 B/s accumulated 26,495 B, then one queue-pressure close occurred.
  The worker saw 3,289 `WOULD_BLOCK`, 312 `sigio`, 4,710 TX wakes, and zero
  socket/stall error. Event scheduling is therefore not the remaining
  bottleneck; the pinned Mbed/lwIP/SoftAP data path remains the product gate.

## 2026-07-26 raw Wi-Fi platform boundary

- PlatformIO `ststm32 19.2.0` and Arduino Mbed `4.3.1` are now pinned for the
  CSM M7 build. The effective baseline is Mbed OS 6.17 with lwIP
  `MSS=536`, `SND_BUF=1072`, `WND=2144`, 16 TCP segments, five pbufs,
  16,000 B heap, and a 1,200 B TCP/IP thread stack.
- Permanent blocking and nonblocking+`sigio` raw SoftAP/TCP benchmarks remove
  CSM publisher, queues, CAN, RC, feeder, and product encoding from the path.
  Both 30 s PC runs passed deterministic byte-pattern integrity with no
  unexpected close. Blocking delivered 2,103,264 B at 69,413 B/s average;
  `sigio` delivered 1,970,872 B at 65,083 B/s average.
- Both raw paths still had zero-throughput one-second windows. Blocking
  `send()` reached 1,046,520 us and `sigio` recorded 4,297 ms maximum
  no-progress. This proves the product queue is not the only limiting layer;
  the current Mbed/lwIP/SoftAP path requires a tuned-profile A/B before another
  worker or queue rewrite.
- The handoff's initial production-candidate Mbed values were rebuilt exactly,
  producing `libmbed.a` SHA-256 `CB63A307...B4833A7A`, but even the small raw
  firmware overflowed Portenta `RAM_D2` by 16,403 B. That candidate is rejected
  and was not uploaded or applied to the product build.
- Product readiness remains blocked. The next gate is a reproducible tuned
  Mbed profile derived within the actual linker/RAM budget, followed by
  STA/lwiperf isolation and mixed-load HIL.

## 2026-07-26 deterministic executive closure and 100 fps combined gate

- Built-in CAN TX now has one owner and reports `CAN_TX_RAW` only after FDCAN
  hardware completion. Timeout/cancel/late completion and TX inhibit have
  submission-correlated host coverage.
- Drive/steering releases use an absolute-phase, no-catch-up schedule. Source
  valid-to-stale transitions prepare the exact drive stop frame in the same
  service call.
- Feeder boot/session, packet/frame ordering, DMA cursor ambiguity, readiness,
  and fault evidence now fail closed. The normal feeder firmware remained at
  3,000/3,000 frames in the 100 fps full-standard-ID run with zero sequence,
  payload, MCP, ring, or UART failure.
- The final MDPS bench build passed all host contracts/guards, used
  448,408/523,624 B RAM and 372,608/786,432 B flash, and was uploaded to COM7.
- The simultaneous 30 s CSM gate passed one boot session, CRC and all
  typed/segment/capture continuity, feeder/CAN/USB loss, and critical-fault
  checks. Wi-Fi did not pass: the current AP/TCP path drained about 13 kB/s,
  below the mixed 200 Hz `CAN_TX_RAW` plus 100 fps RX evidence rate, and the
  508-normal-descriptor boundary isolated the client. A separate 1 ms,
  four-write/4 KiB bounded Wi-Fi pump improved delivered data from 52,892 B to
  282,923 B before isolation but did not remove the steady deficit.
- The queue and pressure-close policy are intentionally unchanged. The next
  gate is a direct socket-throughput characterization followed by either a
  proven worker/driver correction or a deliberate evidence encoding/transport
  decision. 2,000 fps combined HIL, fault injection, and soak are blocked on
  that result; no product Wi-Fi completion is claimed.

## 2026-07-24 RP2040 CAN feeder ingress gate

- Adafruit Feather RP2040 CAN이 bus0의 MCP25625/SPI ingest와 CAN ACK를 단독
  소유한다. M7 direct MCP2515 path는 successor profile에서 compile-out된다.
- feeder core0 CAN ingest와 core1 UART framing을 fixed SPSC ring으로 분리했다.
  CSM은 Mid Carrier J14 `RX2` (`PG9/USART6_RX`)의 circular DMA로 수신하고 기존
  canonical `CAN_RX_SEGMENT`에 admission한다. 링크는 1 Mbaud, COBS + CRC32C,
  boot/packet/frame sequence를 사용하며 observation-only다.
- feeder 단독 2,000 fps/60초에서 120,000/120,000, gap/duplicate/reorder/
  corruption/overflow/bus-off 모두 0이었다.
- 최종 feeder→CSM 2,000 fps/5초에서 시험 frame `0..9999` 10,000개가 CSM USB에
  정확히 도착했다. CSM CAN/segment/USB drop, typed/segment/capture gap,
  feeder UART write/MCP/ring fault는 모두 0이고 boot session은 하나였다.
- 4 Mbaud는 bench jumper에서 H7 framing error가 재현되어 폐기했다. 실측 약
  62.5 kB/s를 수용하는 1 Mbaud 8N1(100 kB/s)을 현재 검증 계약으로 고정한다.
- 현재 통과 범위는 bus0 feeder vertical slice다. J4 RC/MDPS 동시부하와 Wi-Fi
  observer를 포함한 장시간 product gate는 아직 남아 있다.

## 2026-07-23 nonblocking Wi-Fi fanout closure

- The producer-side yield retry was discarded. Canonical main/RC/CAN enqueue is
  a nonblocking SPSC path; only the socket worker owns send/receive/close/abort.
- Bench evidence isolated the reconnect cause as `WifiCloseReason=6`: the old
  252-descriptor queue reached its 75% descriptor threshold while the 48 KiB
  byte pool was only about 36% occupied. It was not a board reset, watchdog,
  socket error, Android ingress overflow, or CAN/RC stall.
- Product Wi-Fi profiles now use 512 descriptors and a 48 KiB byte pool. Both
  high-water dimensions represent about 2.5 s for the measured ~95 B/record
  mix; descriptor counters are `uint16_t` and have a >255 contract test.
- `BOARD_EVENT 45` retains the authoritative close reason and disconnect count.
  The main loop merges both sink-service polls so event pulses are not lost.
- Final physical H7 + `SM-S936N` + USB observation held one TCP connection for
  95 s: disconnect/stall/socket/overflow/pressure-close all zero, queue byte
  high-water 22,759/49,152, and main-loop max gap 5,069 us.

## 2026-07-23 RC/Service shared vehicle bench contract

- RC source is CRSF CH2(index 1), positive forward. RC and Android Service/HIL now share standard `0x005` DLC8 drive (`AA 52 speedLE direction 00 00 00`, stop `AA 02 00 00 00 00 00 00`) and standard `0x007` DLC8 steering byte0 `10..130..250`/zero tail.
- Drive uses a 5% joystick deadband. Above it, the first active command is 20% (`speed=200`) and subsequent output is rounded to 5% steps through 100%; values `1..199` are never emitted with mode `AA 52`. The existing time-equivalent slew and zero-before-reverse rule remain, with drive configured at 200 Hz and steering independently at 50 Hz. Co-scheduled drive/steering frames are drained into the CAN FIFO in one bounded service pass instead of retaining steering across the next 5 ms deadline. Unqualified/lost/failsafe RC emits only the drive stop frame when autonomy is explicitly released and hard/hardware gates are healthy.
- Service/HIL permits RC or host through the same authority boundary, never both as motion owners. Host allowlist validates exact ID/DLC/payload. Normal app joystick release keeps ARM while sending neutral; lifecycle/session/authority failures still disarm.
- Wi-Fi identity uses a connection-edge `STREAM_SESSION` anchor, with an
  idempotent `HOST_QUERY_CAPABILITY` recovery handshake ordered as
  `STREAM_SESSION -> CAPABILITY -> CONTROL_ACK`; there is no periodic session
  record. Service/HIL CAN RX segmentation is 20 ms instead of 1 ms.
  Remote-control and uplink host contracts pass; live upload/reconnect
  verification, 3-minute soak, and external CAN cadence remain open.

## 2026-07-22 accepted socket 수명 결함 판정

- 실제 reset `ref.bin`과 현재 symbol build는 SHA-256 `06AA81E4AD4E696C40610E1F9D0322667729235008DD10EE2180727D592C9866`으로 동일하다. 이 binary는 Mbed factory-accepted `TCPSocket`에 `close()`를 호출한 뒤 deleting destructor를 다시 호출한다. Mbed 계약상 `close()`가 이미 객체를 해제하므로 확정적인 double-destruction/undefined behavior 결함이다.
- 실보드 retained evidence도 `CloseClient` 반환 직후 `DeleteClient` 진입, 미완료 상태에서 reset된 것을 보존했다. 따라서 이 결함은 관측 reset의 고신뢰 촉발 원인이다. raw reset latch가 0이므로 HardFault/watchdog 중 최종 reset executor는 아직 확정하지 않는다.
- accepted client 종료를 close-only로 수정하고 위험한 `delete owned`를 요구하던 architecture guard를 반대로 금지하도록 교정했다. phase 9/12는 과거 evidence 해석용 reserved 값으로 유지한다.
- 이전 REF/A/B/C 결과는 모두 `wifi_connect +0`, `wifi_sent +0`이므로 `USB_ONLY_IDLE / SYMPTOM_NOT_OBSERVED`로 강등한다. connected/reconnect 안정성 근거가 아니다.
- close-only REF는 실제 SM-S936N observer에서 성공한 reconnect 20회(정상 중지 1회, process stop 19회)를 포함한 600초 동안 boot sequence 6과 단일 session을 유지했다. CSM counter는 connect/disconnect `+22/+22`, Wi-Fi sent `+857`이며 CRC/gap/USB disconnect/quarantine/runtime contract error는 모두 0이다. 이 재현 결함은 `FIXED`로 판정하며 동시 HIL과 soak는 아직 남아 있다.
- I2 20초 preflight는 RC valid frame `+1987`, Wi-Fi sent `+320`, USB sent `+310`, 동일 boot/session, CRC/gap 0을 증명했지만 Kvaser physical channel 0은 ACK 0이고 CSM bus0/bus1 RX도 0이었다. 따라서 동시부하는 CAN 물리 경계의 `INVALID_LOAD`이며 reset 재발로 판정하지 않는다.

## 2026-07-22 crash-first self-debug 현재 상태

- `RuntimeSupervisor`와 `BootRecovery`가 risky driver보다 먼저 시작한다. backup
  SRAM `512..2815`에 교대 metadata 2슬롯과 compact event 64개 ring을
  checksum-last로 commit하고, boot sequence/last progress/30초 stable/early-reset/
  Wi-Fi quarantine을 `BOARD_HEALTH v13`에 투영한다. `3072..3199`의
  `RetainedCallLatch`는 Wi-Fi vendor call 진입 전과 반환 직후를 별도 보존한다.
- 같은 source/experiment selector에서 30초 이전 종료가 연속 2회 복구되면 다음 boot의 Wi-Fi를
  Off로 낮춘다. build timestamp만 바뀌어도 이력을 지우지 않으며 REF/A/B/C
  selector 변경은 새 구성에 한 번의 clean trial을 준다.
- reset matrix는 REF=`watchdog On+Full`, A=`watchdog On+Wi-Fi Off`,
  B=`watchdog Off+Full`, C=`watchdog On+AP-only`다. 네 artifact 모두 source hash
  `77bf1341a672984d8eb3d770234e3d2f7acf0957036a8eb705deadd997517856`로
  build됐고 application-data CAN TX는 compile-time 차단됐다.
- 2026-07-22 Portenta COM7의 REF/A/B/C 180초 결과는 단일 boot session, CRC 0,
  sequence gap 0이었지만 네 결과 모두 `wifi_connect +0`, `wifi_sent +0`이었다.
  따라서 `USB_ONLY_IDLE / SYMPTOM_NOT_OBSERVED`이며 Wi-Fi reset matrix 합격이 아니다.
- reset raw는 여전히 0/unknown이었다. Arduino bootloader가 application보다 먼저
  RCC latch를 지울 수 있으므로 bootloader early capture 또는 외부 power/reset
  evidence 전에는 watchdog/power/hard fault를 확정하지 않는다.
- 선택된 PlatformIO env와 material flag는 deterministic runtime contract ID로
  source ID/selector와 합성되어 recovery identity를 이룬다.

## 2026-07-21 Wi-Fi 실행 경계 리팩터링

- `WifiTcpSink`에서 모든 AP/socket 호출과 socket lifetime을 제거하고,
  단일 `WifiSocketWorker`가 configure/AP/server/accept/send/recv/close와 socket
  lifetime을 소유하도록 분리했다. accepted factory socket은 close-only다.
- main/publisher 경계는 bounded TX/RX mailbox의 try-offer와 cached snapshot만
  사용한다. 250 ms 이상 진행 중인 호출은 phase/sequence/heartbeat로 관측한다.
  같은 M7/kernel/WHD/radio/전원을 공유하므로 vendor stall과 power fault까지
  물리 격리됐다고 주장하지 않는다.
- Wi-Fi runtime은 Disabled/AP-only/Full TCP로 분리하고 startup attempt를 기본
  1회로 제한했다. 이 제한은 한 번의 `beginAP()` 반환 시간을 보장하지 않는다.
- runtime diagnostic schema 2는 Wi-Fi call phase/sequence/start/duration/result,
  worker heartbeat, stall count, epoch를 포함한다. retained evidence는 Portenta
  bootloader heap과 겹치던 일반 RAM 대신 STM32H747 backup SRAM을 사용한다.
- boot recovery/runtime supervisor/Wi-Fi isolation/remote control/BOARD_HEALTH v13/
  canonical fanout host contract와 architecture/source/profile guard가 통과했다.
  blocked-client/RC/USB 동시 HIL은 다음 gate다.

## 2026-07-20 실장비 결과

- Arduino `WiFiClient`의 내부 RX thread/동기 종료 경로를 제거하고, 단일 owner의 nonblocking raw Mbed `TCPSocket`과 고정 downlink buffer로 Wi-Fi sink를 구현했다. `BOARD_HEALTH v11`은 socket timing과 이전 runtime breadcrumb를 제공한다.
- 실제 Portenta H7 Service/HIL, `SM-S936N` Android, USB, Kvaser CAN1 20 Hz 동시 시험에서 12초 preflight와 180초 창이 통과했다. 180초 동안 CAN `+3580`, USB `+3776`, Wi-Fi `+3776`, CRC/gap/drop/overflow/MCP/socket/stall delta는 모두 0이었다.
- 이어진 300초 창은 동일 boot session, CAN truth 무손실, Wi-Fi 오류 0을 유지했지만 MCP2515 SPI `+3`, error flag `+1` 때문에 strict gate는 실패했다. event context는 MCP status read의 `CANINTF=0xFF`였고 FIFO overflow, CAN drop, sequence gap은 0이었다.
- 이후 무인 운용 중 red LED와 Android 재연결이 발생했고 boot session이 `0x20E6A7BEEE396122`에서 `0xB24FC5C68F20DA5B`로 바뀌어 CSM 재부팅으로 판정했다. 새 boot의 Wi-Fi disconnect/stall/socket delta는 0이고 retained breadcrumb가 남지 않아 hard reset 또는 power-path interruption이 open risk다.

## 저장소 기준선

- 원격 저장소: `https://github.com/JO-DOHYUN/csm_remote.git`
- 분기 기준 commit: `d2a1f87fae3e44ba978f9f483dd4b8799cee32ad`
- 작업 branch: `codex/vsm-wifi-fanout`
- 정식 PlatformIO project: `firmware/csm`

## 확인된 현재 사실

- 2026-07-22 PCAN/J4에서 RC/MDPS 벤치 매핑을 실측했다. 30초간 ID `0x007` 1,452개, 주기 중앙값 20.693 ms/최대 21.761 ms였고, CH5 독점 byte0..6 `0x00` + byte7 `0x01/0x80`, CH10/CH11 조향 유지 + byte7 오버레이를 모두 확인했다. 이는 실제 송신 payload/주기 증거이며 별도 송신원 대비 CAN 무손실 증명은 아니다.

- passive M7 env `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`가 clean clone에서 build된다.
- 제품 M4 env `portenta_h7_m4_remote_frontend`는 Serial3 CRSF 416666 8N1, 16채널/링크 통계, 정규화, 양방향 telemetry와 SRAM4 IPC를 포함해 build된다.
- 제품 M7 env `portenta_h7_m7_mid_mcp2515_j4_remote_product_wifi`는 RC authority/limiter 관측 경계, built-in CAN bus 1 RX, canonical USB/Wi-Fi evidence를 포함한다. mapper는 `None`, local CAN TX capability는 Off다.
- vehicle 실차 전 벤치 env `portenta_h7_m7_mid_mcp2515_j4_remote_product_mdps_bench_wifi`는 같은 제품 authority/safety/fanout을 유지하고 `VehicleBench0x005And0x007`의 J4 송신만 연다. MCP2515는 normal-mode RX/ACK만 허용하며 host/control TX는 계속 금지한다.
- 2026-07-22 REF 15초 실측에서 J4 `+300`, RC valid/accepted `+1487`, boot session 변화와 CAN/USB drop은 0이었다. 당시 RC 출력 부재는 수신 고장이 아니라 REF의 `BOARD_DIAG_SUPPRESS_REMOTE_CAN_TX=1` 때문이었다.
- remote product architecture, Phase 2A, Phase 2B guard와 전체 runtime 계약시험이 통과한다.
- hard safety 뒤 upstream autonomy가 `InactiveConfirmed`로 release해야 RC reservation을 평가한다. 그 뒤 500 ms 중립 qualification, 정상 stale/failsafe의 즉시 중립과 1초 release를 적용하며 malformed/protocol/IPC/M4 failure는 release하지 않는다. autonomy runtime wiring은 아직 없다.
- SRAM4 schema 2 IPC는 header, M4→M7, M7→M4를 32-byte cache-line 단독 소유 영역으로 분리한다. M4/M7 artifact는 항상 한 쌍으로 배포한다.
- typed v1 frame은 유지하면서 `CanonicalPublisher`가 fanout 전에 `publish_seq64`를 배정하고 `seq u16`에 하위 16비트를 기록한다. `STREAM_SESSION`이 full identity를 고정한다.
- `TypedRecords.h`가 CAN raw/segment와 `BOARD_HEALTH v13` field offset constants를
  제공하며 v13은 v12의 472-byte prefix를 그대로 보존한다.
- `RecordAdmission`, one-encode `CanonicalPublisher`, fixed `UsbCdcSink`, fixed `WifiTcpSink` dual fanout이 구현되어 있다. Wi-Fi sink는 49,152 B/256-descriptor live FIFO, 4-record/2,112 B critical reserve와 32,768 B/192-record 관측 high-water를 사용하며 accepted/sent/aborted는 내부 64-bit 보존식으로 관리한다. high-water는 close 조건이 아니다.
- 2026-07-30 최종 feeder product M7 build는 RAM 170,848/523,624 B(32.6%), Flash 367,392/786,432 B(46.7%)였고 COM7 DFU 업로드가 성공했다. 15초 USB+PC Wi-Fi gate는 4,098 records, bad CRC/gap 0, Wi-Fi overflow/stall/socket error 0, 보존식 residual 0으로 통과했다.
- Wi-Fi observer env는 Arduino Mbed Wi-Fi AP direct와 TCP server `192.168.4.1:3333`, client 1개를 사용한다.
- host fanout contract test, passive M7 build, Wi-Fi observer M7 build, M4 frontend proof/probe와 passive symbol guard가 통과했다.
- 2026-07-15 D-016 Android diagnostics binding 변경 후 host fanout contract와 passive/Wi-Fi observer M7 build를 다시 통과했다.
- 2026-07-16 현재 Wi-Fi observer M7 회귀 build 사용량은 RAM 74.6%/Flash 46.3%다.
- 2026-07-15 COM7 1200-bps touch로 DFU 전환 후 Wi-Fi observer firmware 실제 업로드가 성공했다. 수동 double-reset은 필요하지 않았다.
- 실제 보드 AP `VSM-CSM-DEV`, Android TCP client, `STREAM_SESSION`, `BOARD_HEALTH` 수신이 확인됐다.
- connection poll을 session gate보다 먼저 수행하도록 수정했고 Wi-Fi stalled-client close 기준을 5 s로 조정했다. 최종 60 s status-stream에서 epoch 1, connect 1, disconnect/stall/overflow 0이었다.
- 2026-07-16 Service/HIL 실측에서 비활성 encoder가 0값 `ENC_DERIVED`를 계속 발행하고 disabled timer를 fault로 광고하던 계약을 제거했다. 해당 profile은 encoder capability도 0으로 광고한다.
- Kvaser CAN1 `0x50` 20 Hz와 CSM Wi-Fi를 같은 30 s 창에서 계측한 최종 artifact는 `C:\WORKS\VS\vsm_android_app\build\hil\20260716-114703-kvaser-can1-wifi-observer`다. CSM health-window source/bus0는 `580/580`, CAN/FIFO/Wi-Fi drop과 typed/segment/capture gap은 모두 0이었다.
- 2026-07-16의 폐기 전 `0x100/0x200` 계약에서는 PC HIL client로 canonical heartbeat/ARM/neutral/disarm과 matching `CAN_TX_RAW`를 확인했다. 이 기록은 D-016의 현행 `0x005/0x007` 검증으로 승계되지 않는다.
- 2026-07-20 당시 dirty build 사용량은 M4 RAM 14.7%/Flash 7.0%, M7 RAM 74.7%/Flash 44.0%였다. 현재 reset REF build는 RAM 79.5%/Flash 45.5%다.
- 실제 R16SM CRSF/telemetry, M4-M7 IPC, autonomy wiring, 실제 vehicle mapping, D1 hardware gate, completion-correlated CAN TX, production Wi-Fi 동시 운용, dual-CAN 고부하와 장시간 reset gate는 아직 검증되지 않았다.

## 확정된 목표

- M4 RC frontend, M7 단일 authority/CAN TX owner를 유지한다.
- M7 evidence를 canonical publisher에서 한 번 직렬화한다.
- USB CDC와 Wi-Fi TCP는 bounded 독립 sink로 fanout한다.
- Wi-Fi 1차 제품은 Android observer 한 대, live-only, reconnect 시 새 epoch,
  fresh current `STREAM_SESSION`, board backlog replay 없음이다.
- Wi-Fi는 256-descriptor/49,152-byte FIFO에서 positive send 즉시 release하며,
  실제 reserve/full admission miss 또는 5초 no-progress에서만 해당 epoch를
  close+flush한다.
- Android Capture 실패는 앱 내부 PARTIAL/CORRUPT이며 CSM/TCP/Live를
  제어하지 않는다.
- Wi-Fi는 safety/RC/CAN/USB 뒤에 시작한다.
- Windows USB observer와 Android Wi-Fi observer는 RC 운용 중 동시에 사용할 수 있어야 한다.

## 다음 qualification gate

1. 실제 Android에서 positive-send release, no-backlog reconnect, fresh
   `STREAM_SESSION`을 검증한다.
2. queue overflow/socket stall/Capture storage failure를 주입해 Wi-Fi만
   close+flush되고 RC/CAN/USB가 지속되는지 검증한다.
3. Windows USB + Android Wi-Fi + RC + CAN0 2,000 fps + CAN1 2,000 fps 동시 HIL과
   1/8/24시간 soak를 수행한다.
4. R16SM CRSF/telemetry와 M4-M7 IPC를 실제 장비에서 검증한다.
5. upstream autonomy runtime profile, 실제 차량 mapping, D1 hardware gate를 승인한다.
6. FDCAN completion-correlated `CAN_TX_RAW` host contract는 완료했다. 실제
   Kvaser ID/payload/주기/ACK와 late/cancel failure HIL은 남아 있다.

Android 전용 wire 형식이나 sink별 별도 encode 경로는 만들지 않는다.
