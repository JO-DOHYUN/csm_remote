# CSM Remote 결정 이력

## D-001 단일 하네스 권위

- 결정: 루트 `AGENTS.md`와 `BRIEF.md`에서 작업 문서 하나로 직접 라우팅한다.
- 이유: 중첩 AGENTS, imported prompt, 중복 product/architecture 문서가 서로 다른 시대의 계약을 활성화했다.
- 폐기: `docs/remote`, 모든 nested `AGENTS.md`, firmware 내부 중복 brief/harness/prompt.
- 보존: 실제 firmware, guard, build env, canonical wire, hardware evidence.

## D-002 제품 topology와 권한

- 결정: M4 RC frontend, M7 단일 authority/safety/CAN TX owner를 유지한다.
- 우선순위: hard safety > upstream autonomy > RC > service host > monitoring.
- production VSM은 Windows와 Android 모두 observer-only다.

## D-003 canonical publisher fanout

- 결정: typed record를 sink 앞에서 한 번 순서화·직렬화하고 USB CDC와 Wi-Fi TCP의 bounded 독립 sink로 fanout한다.
- 이유: transport별 별도 encode/order는 무결성 비교를 깨고, 단일 blocking queue는 RC와 다른 sink에 장애를 전파한다.
- 금지: USB staging이 canonical identity를 소유하는 구조, sink별 임의 record 변형, 무제한 backlog.

## D-004 Wi-Fi 1차 범위

- 결정: Android observer 1대, live-only, reconnect 새 epoch, CSM backlog replay 없음.
- 이유: CSM의 제어·evidence capture 책임을 보존하면서 memory와 복구 의미를 bounded하게 유지한다.
- 향후 다중 client는 별도 제품 변경 심사를 거친다.

## D-005 identity 분리

- 결정: CAN capture, segment, canonical publish, sink delivery, app capture identity를 분리한다.
- 결정: v1 frame layout을 유지하고 `seq u16`을 fanout 전 `publish_seq64`의 하위 16비트로 사용한다.
- 결정: record 17 `STREAM_SESSION`이 `boot_session_id`, full `publish_seq64`, reason을 boot, sink epoch, wrap 시점에 제공한다.
- 구현: publisher 1회 encode 후 sink-owned bounded queue로 복사한다. 두 sink만 존재하는 제품에서 shared refcount pool보다 작고 장애 격리가 명확하다.
- 제한: Wi-Fi 전용 envelope와 sink별 재직렬화를 금지한다.

## D-006 저장소 기준선

- 기준: GitHub `JO-DOHYUN/csm_remote`, commit `51b411191aa7410df9b2bfecbddd040451287db0`.
- 개발 branch: `codex/vsm-wifi-fanout`.
- 기존 로컬 `C:\WORKS\VS\csm_remote` dirty tree는 변경하지 않는다.

## D-007 Wi-Fi observer 구현 profile

- 결정: `portenta_h7_m7_mid_mcp2515_j4_dual_csm_observer_wifi`는 passive product 안전 플래그를 상속하고 Wi-Fi AP/TCP sink만 추가한다.
- 네트워크: 개발 AP `VSM-CSM-DEV`, `192.168.4.1:3333`, client 1개, backlog replay 없음.
- 격리: Wi-Fi와 USB는 각각 8-record fixed queue를 소유하며 publisher frame을 복사한 뒤 독립 배출한다.
- build 격리: 비활성 profile은 `WifiTcpSinkDisabled`만 링크하여 Wi-Fi library와 sink queue 비용을 갖지 않는다.
- 제한: 개발 credential은 production provisioning 결정이 아니며, 실제 보드 upload/AP/HIL proof가 남아 있다.

## D-008 Wi-Fi connection admission과 stall 판정

- 날짜: 2026-07-15
- 상태: Active, actual-device status-stream 검증 완료.
- 결정: sink connection poll을 session publication gate보다 먼저 수행한다. Wi-Fi nonblocking write의 일시적인 0 반환은 5 s 동안 진행이 없을 때만 stalled-client close로 판정한다.
- 이유: accept 전 `hasConnectedSink()` early return은 TCP handshake만 완료하고 application accept를 막았다. 1 s stall 기준은 Android 수신 중 정상적인 Mbed socket backpressure도 반복 disconnect로 오판했다.
- 보존 경계: write와 queue는 bounded/nonblocking이고 Wi-Fi sink만 close된다. RC, CAN ingest, publisher, USB sink는 기다리지 않는다.
- 실측: SM-S936N Android observer 연결에서 60 s 동안 Wi-Fi epoch/reconnect 변화 0, CSM `wifi_disconnect=0`, `wifi_stall_close=0`, `wifi_overflow=0`, 앱 CRC/length/sequence/ingress 오류 0이었다.
- 제한: 이 결과는 idle/status stream gate다. dual-CAN 고부하, blocked client, USB 동시 수신, RC timing HIL은 별도 gate다.

## D-009 RC 최우선 vertical slice

- 날짜: 2026-07-20
- 상태: M4/IPC/authority skeleton은 Active. vehicle mapping과 product TX 주장은
  D-012로 폐기·교정.
- 결정: M4는 Portenta `Serial3`의 CRSF 416666 8N1 수신, CRC/16채널/링크 통계/정규화와 수신기 telemetry만 소유한다. M7은 authority, safety, limiter, vehicle mapping, CAN driver와 송신 evidence를 단독 소유한다.
- IPC: OpenAMP/RPC를 제거하고 SRAM4 `0x38000000`의 1 KiB 고정 window를 version 2 double buffer와 checksum/commit sequence로 사용한다. header, M4→M7, M7→M4를 32-byte cache-line 경계의 단독 소유 영역으로 분리하고 각 writer는 자기 영역만 clean한다. M4/M7 artifact는 항상 한 쌍으로 배포한다.
- 교정: 당시 CH2/CH4와 5-ID 송신 구상은 승인된 차량 mapping이 아니었다.
  현재 explicit mapper는 bench용 CH4 `0x007` 하나뿐이고 product 기본값은
  `None`이다. autonomy release 뒤 500 ms 중립 qualification과 정상 loss의
  1초 release 계약만 보존한다.
- CAN: Remote Product의 local CAN TX는 기본 Off다. built-in driver의 양수 write
  결과는 FIFO enqueue 수락이며 completion-correlated actual-send evidence가 아니다.
- profile: production remote profile은 host/app CAN downlink를 compile-time 제거한다. 향후 Android 2순위 제어는 별도 Service/HIL profile에서 동일 authority boundary와 neutral handoff를 통과해야 한다.
- 상태: RC observation 코드와 host 계약시험은 존재한다. autonomy wiring, 실제
  mapping, hardware gate와 외부 CAN HIL 전에는 release 완료로 주장하지 않는다.

## D-010 Wi-Fi socket 단일 소유와 watchdog 진단 경계

- 날짜: 2026-07-21
- 상태: Active, D-011의 crash-first recovery 경계로 확장.
- 결정: `WifiTcpSink`는 publisher-facing nonblocking facade와 cached 상태만
  소유한다. AP 초기화, server accept, socket 설정, send, recv, close,
  socket lifetime은 하나의 `WifiSocketWorker`만 소유한다. Mbed `accept()`가 만든
  factory socket은 `close()` 자체가 해제하므로 별도 delete하지 않는다.
- 격리: TX/RX bounded mailbox와 generation으로 application lock과 socket
  lifetime을 main/RC/CAN/USB에서 분리한다. 250 ms 이상 진행 중인 호출은
  관측·표시하되 worker thread를 강제 종료하거나 board reset을 요청하지 않는다.
  같은 M7/kernel/WHD/radio/전원을 공유하므로 vendor call과 power fault까지 물리
  격리한다고 주장하지 않는다.
- 진단: worker는 record/retained 저장을 직접 수행하지 않고 호출 전·후
  snapshot만 게시한다. main runtime diagnostic schema 2가 phase, sequence,
  start, duration, result, heartbeat, stall count, epoch를 100 ms 주기로
  기록하며 double-buffer retained slot은 bootloader heap이 아닌 H747 backup
  SRAM에 둔다.
- 한계: RCC reset cause는 현재 Arduino bootloader가 먼저 읽고 지울 수 있어
  application raw register만으로 확정하지 않는다. 이번 경계는 reset 직전
  실행 위치와 Wi-Fi call stall 여부를 보존하며, 정확한 reset-source 보존은
  필요 시 bootloader early capture gate로 별도 처리한다.

## D-011 crash-first self-debug와 boot-loop 복구

- 날짜: 2026-07-21
- 상태: Retained evidence 경계는 Active. Wi-Fi startup quarantine 결정은
  D-029로 폐기.
- 결정: `RuntimeSupervisor`를 위험 driver보다 먼저 시작하고 platform-independent
  `BootRecovery`가 retained metadata, compact event ring, early-reset 분류와
  Wi-Fi startup decision을 소유한다. main은 board adapter와 driver 적용만 한다.
- 저장: H747 backup SRAM `0..383`은 legacy/deep diagnostics로 보존하고,
  `512..2815`는 checksum-last metadata 2슬롯과 32 B event 64개 ring으로 고정한다.
  `3072..3199`는 Wi-Fi vendor call 전/후를 보존하는 dual-slot
  `RetainedCallLatch`다. torn write는 반대 metadata 슬롯로 복구하고 corruption
  counter를 숨기지 않는다.
- 안정/복구: 동일 source/experiment selector가 30초 stable marker 전에 연속 2회 종료되면
  다음 boot의 effective Wi-Fi mode를 Off로 내려 USB와 retained evidence를
  우선 살린다. build timestamp 변경은 이력을 지우지 않고 source manifest 또는
  experiment selector 변경만 한 번의 clean trial을 준다.
- profile: REF=`watchdog On + Full`, A=`watchdog On + Wi-Fi Off`,
  B=`watchdog Off + Full`, C=`watchdog On + AP-only`다. 동일 source set을 link하고
  selector만 바꾸며 모두 application-data CAN TX를 차단한다. CAN controller의
  ACK/error signaling까지 물리 차단한다는 뜻은 아니다.
- evidence: source hash
  `77bf1341a672984d8eb3d770234e3d2f7acf0957036a8eb705deadd997517856`의 네
  artifact가 각각 단일 boot session, 30초 stable, CRC/gap/application CAN TX 0으로
  180초 통과했다. REF/A/C는 watchdog 실제 3000 ms, B는 실제 비동작을 확인했다.
  과거 reset 원인은 이 1차 통과만으로 Wi-Fi 또는 watchdog으로 확정하지 않는다.
- release 정책: `RuntimeSupervisor`, compact retained ring, `BOARD_HEALTH` recovery
  scalar와 loss/failure counter는 제품에 남긴다. 100 ms runtime record, FDCAN/Wi-Fi
  상세 snapshot과 A/B/C matrix는 instrumented artifact로 제한한다.
- 제한: application보다 먼저 reset latch를 지울 수 있는 bootloader 때문에 정확한
  reset source는 아직 open이다. bootloader early latch 또는 외부 power/reset
  evidence 없이는 watchdog/power/hard fault를 확정하지 않는다. 선택된 PlatformIO
  env/material flag는 deterministic runtime contract ID로 recovery identity에
  포함한다.

## D-012 product control 승인 경계 교정

- 날짜: 2026-07-21
- 상태: Active, fail-closed.
- 결정: 권한 순서는 `hard safety > upstream autonomy > RC > service host >
  monitoring`이다. autonomy가 `InactiveConfirmed`가 아니면 RC presence와
  무관하게 local motion을 거부한다.
- mapping: Remote Product의 mapper는 `None`, local CAN TX capability는 Off다.
  `MdpsBench0x007`은 명시적 bench flag로만 사용하며 5-ID product mapping 주장을
  폐기한다.
- TX evidence: `CONTROL_ACK`와 `CAN_TX_RAW`를 분리한다. built-in driver enqueue
  수락 직후 record는 physical TX proof가 아니며 FDCAN TX completion/TXBTO와
  correlation한 record, Kvaser 등 외부 analyzer가 함께 일치해야 actual send로
  승인한다.
- release open gate: bootloader reset latch, autonomy runtime wiring, 실제 vehicle
  mapping, D1 hardware gate 극성·readback, completion-correlated `CAN_TX_RAW`, 외부
  analyzer HIL, RC+CAN+USB+Wi-Fi fault/soak.

## D-013 Mbed accepted socket close-only ownership

- 날짜: 2026-07-22
- 상태: Active, 실보드 reconnect 회귀 통과. 동시 HIL/soak 대기.
- 결함: `TCPSocket::accept()`가 반환한 factory object에 `close()` 후 `delete`를 다시 호출했다. ArduinoCore-mbed 4.3.1의 계약은 close가 객체를 deallocate하고 이후 포인터 참조는 undefined behavior라고 명시한다.
- binary evidence: reset에 사용된 `ref.bin`과 symbol build의 SHA-256은 `06AA81E4AD4E696C40610E1F9D0322667729235008DD10EE2180727D592C9866`으로 동일하다. 해당 binary는 worker close 뒤 deleting destructor를 다시 호출하며, Mbed close 내부도 factory object deleting destructor를 호출한다.
- runtime evidence: 두 early reset 중 마지막 retained call은 `CloseClient` 반환 직후 `DeleteClient` 진입, 미완료 상태였다. 이 결함은 reset과 고신뢰로 연결되지만 reset executor가 HardFault인지 watchdog인지는 raw latch 0으로 미확정이다.
- 결정: accepted socket은 close-only로 종료한다. phase 9/12는 과거 retained evidence 해석을 위해 reserved로 남기고 재사용하지 않는다. architecture guard는 explicit accepted-socket delete를 금지한다.
- 검증: close-only REF를 SM-S936N observer와 600초 운용하며 성공한 reconnect 20회(정상 중지 1회, process stop 19회)를 수행했다. CSM은 connect/disconnect `+22/+22`, Wi-Fi sent `+857`, boot sequence 6과 단일 session, CRC/gap/USB disconnect/quarantine/runtime contract error 0을 기록했다. 재현 결함은 FIXED로 판정하고 USB/CAN/RC 동시 HIL로 승격한다.

## D-014 Remote Product MDPS bench artifact

- 2026-07-22 RC 시퀀스 확정: CH5는 독점 시퀀스로 byte0..6을 `0x00`, byte7을 음수 `0x01`/양수 `0x80`으로 송신한다. CH10은 조향을 유지하고 byte7만 음수 `0x01`/양수 `0x80`으로 덮으며, CH11은 평상시 음수 입력을 무시하고 양수일 때만 조향을 유지한 채 byte7 `0x01`을 송신한다. 우선순위는 CH5 > CH11 양수 > CH10이다.
- PCAN/J4 30초 실측에서 ID `0x007` 1,452개, 주기 중앙값 20.693 ms/최대 21.761 ms였고, CH5 독점 `00 00 00 00 00 00 00 01/80`, 일반 조향, CH10/CH11 `조향 byte0 + byte7 01/80`를 모두 확인했다. 이는 실제 송신 payload/주기 증거이며 별도 송신원 대비 CAN 수신 무손실 증명은 아니다.

- 날짜: 2026-07-22
- 상태: Active, bench-only. 실제 보드 업로드와 20초 RC/J4 무손실 관찰 완료; MDPS motion과 MCP 물리-bus HIL 대기.
- 근거: reset REF 15초 실측에서 J4 `+300`, RC valid/accepted `+1487`, boot 변화와 CAN/USB drop은 0이었다. RC 입력은 정상이며 REF가 application CAN TX를 명시적으로 억제하고 기본 제품 profile이 MCP2515를 compile-out한 상태였다.
- 결정: 기본 Remote Product의 fail-closed mapper `None`은 유지한다. 별도 `portenta_h7_m7_mid_mcp2515_j4_remote_product_mdps_bench_wifi`만 제품 authority/safety/limiter 아래 J4 `MdpsBench0x007` 송신을 연다. MCP2515는 normal-mode RX/ACK만 허용하며 MCP control TX, 모든 host TX/downlink는 금지한다.
- wiring: 일반 제품은 `local_tx_inhibit=true`, autonomy `Unknown`으로 유지한다. upstream autonomy가 없는 명시적 MDPS bench artifact만 inhibit를 해제하고 autonomy를 `InactiveConfirmed`로 고정한다. 첫 업로드 관찰에서 이 wiring이 누락되어 RC accepted `+1985`에도 control cycle이 0인 결함을 확인해 교정했다.
- 판정 경계: 이 artifact는 MDPS 단품/차량 벤치용이며 실제 5-ID vehicle mapping 또는 release artifact가 아니다. J4 `0x007` ID/payload/20 ms와 외부 analyzer 관측, MCP error-free ACK, RC/USB/Wi-Fi 동시 부하를 별도로 통과해야 한다.

## D-015 Wi-Fi 정상 부하 무손실 envelope와 active-client 경계

- 날짜: 2026-07-22
- 상태: Active, 60초 I1 실보드 gate 통과. blocked-client/reconnect와 장시간 동시 HIL 대기.
- 결함: active TCP client 중 100 ms마다 extra `accept()`를 호출해 vendor call isolation과 연결 종료를 유발했다. 또한 1 ms CAN segment flush, 512 B TX chunk, 48개 최대-frame 복사 queue가 약 130 CAN frame/s에서 canonical 생산율과 radio burst를 감당하지 못해 Wi-Fi sink loss를 만들었다.
- 결정: active client 중 listener를 호출하지 않는다. queue 수위는 loss evidence일 뿐 close 조건이 아니며, 연결은 연속 5 s socket 무진행·peer close·실제 socket error·명시적 isolation만으로 닫는다. close 진입 전 disconnected state를 publish해 느린 vendor close를 새 call stall로 중복 판정하지 않는다.
- 처리량: CAN truth는 최대 20 ms/15 frame으로 segment화하고, Normal-priority worker가 5 ms 주기에서 최대 1024 B를 nonblocking send한다. sink queue는 48 KiB byte pool+252 descriptor이며 2 KiB+4 descriptor를 critical에 예약한다. 이는 실측 약 8 KiB/s × 5 s timeout에 margin을 둔 정적 envelope다.
- 진단: mailbox admission의 Busy/Reserved/Full/Invalid와 socket WouldBlock/zero write, close reason을 내부에서 분리한다. 기존 aggregate health counter와 epoch/identity는 wire 호환을 유지한다.
- 실측: MDPS bench firmware source `0x27056BF3`, PCAN/J4 약 130 frame/s, PC TCP client 60 s에서 단일 boot sequence 7/session/epoch, 482,493 B/2,847 record를 수신했다. Wi-Fi disconnect/stall/socket error/overflow/no-sink drop, CAN source drop/FIFO, CRC, typed/segment/capture gap이 모두 0이었다. queue high-water는 24,831 B, main-loop max gap은 4,025 us, CAN RX task max는 555 us였다.
- 자원: 제품 build RAM 440,360/523,624 B(84.1%), Flash 357,936/786,432 B(45.5%). 48 KiB를 초과하는 queue 확대는 새 부하 실측과 memory gate 없이는 금지한다.

## D-016 RC/Service 공용 vehicle bench mapping

- 날짜: 2026-07-23
- 상태: 코드/host contract/Service-HIL build 통과, 실보드 HIL 대기.
- 결정: `MdpsBench0x007` 단일 mapper와 Android `0x100/0x200` 임시 adapter를 `VehicleBench0x005And0x007`로 대체한다. RC CH2 positive는 forward이며 drive는 `0x005` DLC8 100 Hz, steering은 `0x007` DLC8 50 Hz다.
- drive 계약: active `AA 52 speed_lo speed_hi direction 00 00 00`, speed `0..1000` little-endian, forward `0x50`, reverse `0x60`; stop `AA 02 00 00 00 00 00 00`. 2% deadband, time-equivalent 50/200 permille limiter와 zero-before-reverse를 RC/host가 공유한다.
- safety neutral: upstream autonomy가 `InactiveConfirmed`이고 hardware/hard-safety gate가 healthy인 명시적 bench에서만 RC invalid/unqualified/failsafe가 0x005 stop을 생성한다. 다른 autonomy state나 local inhibit에서는 silence를 유지한다.
- Service/HIL transport: sink epoch announcement는 해당 sink queue가 수락할 때까지 재시도하며, canonical record 순서를 흔드는 periodic anchor는 사용하지 않는다. CAN_RX_SEGMENT flush는 20 ms로 고정하고 freshness timeout 증가는 금지한다.
- 실측상 Wi-Fi high-water 24,248/49,152 B에서 overflow가 증가해 용량 부족이 아니라 mailbox lock `Busy` 즉시-drop임을 확인했다. producer yield 재시도안은 main/RC/CAN 비대기 원칙 위반으로 폐기하고, TX mailbox를 main-producer/socket-worker-consumer SPSC ownership으로 변경한다. abort만 worker 소유의 nonblocking producer gate를 사용하며 Reserved/Full은 sink miss로 남긴다.
- worker는 byte 또는 descriptor queue 75%에서 해당 Wi-Fi client만 Full 전에 선제 격리한다. 5 s 무진행, socket/RX 오류 정책은 독립 유지하며 재연결은 새 sink epoch와 명시적 loss boundary를 생성한다.

## D-017 Vehicle bench drive cadence correction

- Date: 2026-07-23
- Status: Configured, host-tested, uploaded, and PCAN cadence/payload verified;
  controller-fault-free vehicle HIL remains open.
- New hardware fact: the drive controller is configured for a 5 ms receive
  period. This supersedes D-016's 10 ms drive cadence; the byte contract and
  `0..1000` little-endian scale do not change.
- Decision: J4 `0x005` is scheduled every 5 ms and `0x007` remains independently
  scheduled every 20 ms. CH2 applies the documented 2% deadband before the
  shared limiter and mapper. A due drive/steering pair is submitted to the CAN
  FIFO in one bounded two-frame service pass; it is not retained behind an
  artificial 2 ms gap where the next 5 ms cycle could discard steering.
- Evidence: the 2026-07-23 20 s PCAN/J4 capture observed 3,873 `0x005` frames
  with 5.163 ms median, 5.206 ms p95, 6.168 ms maximum, and zero intervals above
  7.5 ms. It also observed zero PCAN error/status frames, invalid drive payloads,
  unsafe direction changes, runtime deadline misses, and CAN write failures.
  The raw capture SHA-256 is
  `63C0EDCBFFE02213539402645428868E2DF724B01EAF9BD1A91E98DFE94AE460`.
- Evidence boundary: an invalid application payload can cause an ECU fault but
  cannot itself create a physical CAN error frame. The capture accepts CSM
  cadence and bus integrity for this bench run; it does not prove the vehicle
  controller remains fault-free under every motion/load case.

## D-018 Vehicle bench minimum traction command

- Date: 2026-07-23
- Status: Implemented and host-tested; vehicle Encoder Fault retest open.
- Decision: RC CH2 uses a 5% deadband. The drive mapper emits stop through that
  boundary, jumps to minimum `speed=200` for the first active `AA 52` command,
  and quantizes all later speeds to 50-unit steps. It never emits active speeds
  `1..199`.
- Reason: the prior smooth limiter exposed active low-speed values such as
  12/24/36 while the traction controller could still have zero encoder motion.
  This restores the known bench behavior without changing ID, DLC, direction,
  stop payload, cadence, authority, or safety fallback.

## D-019 RP2040 feeder external-CAN source boundary

- Date: 2026-07-24
- Status: Architecture accepted; standalone and feeder-to-CSM throughput gates
  passed, fault-injection and combined soak remain open.
- Decision: external CAN bus0 is ingested by the RP2040 feeder and delivered to
  M7 as a read-only, COBS/CRC32C protected UART source. M7 remains the sole owner
  of authority, canonical identity, product health, and all control TX. The
  accepted bench envelope is 1,000,000 baud and 2,000 CAN frame/s; a higher
  envelope requires a new measured link budget and HIL gate.
- Required evidence: boot ID, packet/frame sequence, CRC, truncation, duplicate,
  reorder, feeder reset, UART/ring overflow, and downstream sink loss are
  distinct counters/events. Feeder or its host telemetry must never block CAN
  ingest.
- Migration rule: the legacy on-CSM MCP/SPI path is removed only after the
  feeder fault and combined-load gates pass; two active owners are forbidden.

## D-020 Deterministic M7 product executive

- Date: 2026-07-24
- Status: Architecture accepted; staged implementation and HIL in progress.
- Decision: control uses one absolute periodic coordinator, one FDCAN owner, and
  one evidence sequencer. Drive 5 ms and steering 20 ms keep fixed phase,
  account exact missed releases, and do not emit catch-up bursts. USB/Wi-Fi are
  independent bounded workers and cannot block authority, CAN ingest, or
  control.
- Evidence semantics: `CAN_TX_RAW` success follows hardware TX completion, not
  FIFO enqueue. Watchdog health is based on ordered subsystem checkpoints and
  control progress rather than a single main-loop heartbeat.
- Migration rule: each staged owner move must add a host/HIL seam and delete or
  explicitly disable the superseded direct path in the same change. No
  second scheduler, queue, or driver owner may coexist as a temporary product
  path.

## D-021 Wi-Fi descriptor envelope follows the actual mixed-record load

- Date: 2026-07-24
- Status: Configured; final target build and concurrent HIL pending.
- Decision: all product Wi-Fi profiles use the measured 48 KiB byte pool plus
  512 descriptors and the 2 KiB/four-descriptor critical reserve. The SPSC
  descriptor stores only `publish_seq`, length/cursor, low 16 bits of the
  monotonic committed byte end, and priority; its size is compile-time fixed at
  16 bytes. Since the byte envelope is below 65,536 bytes, uint16 subtraction
  remains exact at a full ring and across uint32 counter wrap.
- Reason: the product stream now includes 200 Hz individual `CAN_TX_RAW`
  evidence in addition to segmented RX and health records. On the real board,
  252 descriptors reached the 75% record threshold at only about 12.2 KiB and
  caused a queue-pressure close under low CAN load, so the older low-rate
  assumption was false. Removing unused `RecordType` and replacing the 32-bit
  monotonic byte end with its 16-bit low word reduces descriptor storage from
  24 to 16 bytes. `512 x 16` therefore costs only 2,144 bytes more than the
  former `252 x 24` layout, rather than the 6,240-byte cost of an uncompressed
  expansion. Descriptor-tail release remains the sole commit boundary.
- Gate: queue overflow, critical-reserve loss, premature queue-pressure close,
  and RAM use must all be zero/within budget in the final build and simultaneous
  feeder 2,000 frame/s + USB + Wi-Fi HIL.

## D-022 Wi-Fi throughput deficit is a transport gate, not a queue-size gate

- Date: 2026-07-26
- Status: 100 fps combined HIL failed at the Wi-Fi sink; CAN, feeder, USB, and
  canonical integrity passed.
- Evidence: the baseline client received 52,892 B before the 508-normal-record
  reserve boundary isolated it. Giving Wi-Fi an independent 1 ms worker period
  and a bounded four-write/4 KiB pump increased delivery to 282,923 B, but the
  queue still reached the same descriptor boundary after about 20.7 s. The
  capture retained one boot session, zero CRC/typed/segment/capture gaps, zero
  CAN/feeder/USB loss, and zero socket/stall/send-call-budget errors.
- Decision: do not enlarge the 48 KiB/512-descriptor queue or relax
  QueuePressure isolation. Both would only delay a measured steady-state
  deficit. Keep the better measured bounded pump and stop speculative scheduler
  changes.
- Next gate: measure raw board-to-PC TCP capacity independently of canonical
  record production. If the socket path has sufficient margin, correct the
  worker/driver boundary and repeat 100 fps then 2,000 fps. If it does not,
  approve a product-level evidence batching/compression or transport/hardware
  change before further combined-load work.

## D-023 Raw Wi-Fi isolates a platform-path deficit

- Date: 2026-07-26
- Status: Baseline raw AP comparison passed integrity and failed deterministic
  throughput; tuned network-profile comparison remains open.
- Evidence: with product publisher, queues, CAN, RC, feeder, and typed encoding
  compiled out, the pinned Mbed 6.17 baseline delivered 2,103,264 B at
  69,413 B/s average in blocking mode and 1,970,872 B at 65,083 B/s average
  in nonblocking+`sigio` mode over valid 30 s PC runs. Both preserved the
  deterministic byte pattern and connection, but both had zero-throughput
  one-second windows. Blocking `send()` reached 1,046,520 us; `sigio` reached
  4,297 ms without progress.
- Decision: do not enlarge the product queue and do not rewrite the product
  worker again before a reproducible Mbed network-profile A/B. Pin
  `ststm32 19.2.0` and Arduino Mbed `4.3.1`, retain both raw benchmarks, and
  compare a bounded tuned profile against this exact baseline.
- Rejected candidate: the handoff's initial production profile was rebuilt
  against Mbed OS commit `17dc3dc2` and produced `libmbed.a` SHA-256
  `CB63A307...B4833A7A`, but the raw firmware exceeded Portenta `RAM_D2` by
  16,403 B. It was neither uploaded nor promoted. The next profile must be
  derived from the real linker-section budget rather than copied values.
- Boundary: this result implicates the current Mbed/lwIP/SoftAP path but does
  not prove a radio hardware limit or distinguish TCPSocket/netconn, SoftAP,
  WHD/SDIO, and RF. STA and lwiperf/raw-lwIP gates remain required.

## D-024 Event-driven Wi-Fi worker with one canonical diagnostic boundary

- Date: 2026-07-27
- Status: Code/build/upload passed; physical throughput gate failed below the
  required steady rate.
- Decision: keep canonical producer and all socket ownership unchanged, but
  replace the fixed 1 ms worker poll with RTOS event wakes from empty-to-
  nonempty/critical/control/`sigio`, plus a 10 ms connected fallback. A
  successful bounded pump self-wakes while queued data remains.
- Evidence: add only record `20 TRANSPORT_DIAGNOSTIC`, one 128-byte snapshot at
  1 Hz on the existing diagnostic lane. Offer, acceptance, queue, socket,
  progress, wake, epoch, and publication-sequence values identify the first
  losing boundary. No per-call log, second queue, replay, or new runtime owner
  is introduced.
- Gate result: the 30 s canonical diagnostic window measured accepted
  8,943.4 B/s, socket 7,996.7 B/s, 26,495 B backlog growth, 3,289
  `WOULD_BLOCK`, 312 `sigio`, one overflow/queue-pressure close, and zero
  socket/stall errors. D-023 is confirmed for the product path: do not enlarge
  queues or rewrite the worker again. The next approved work is the bounded
  Mbed/lwIP/SoftAP profile/transport decision.

## D-025 Correct the Wi-Fi failure model and freeze the product data plane

- Date: 2026-07-27
- Status: Architecture and cross-host wire contract approved; physical final
  gates are evidence-dependent.
- Correction: D-022 and the final paragraph of D-024 treated the aggregate
  capture window as a steady 946.7 B/s producer/socket deficit. In the valid
  pre-close interval (20.732466 s), accepted was 224,960 B and socket progress
  was 223,819 B: only 55 B/s difference, with repeated queue drain to zero.
  A later ~2.073 s zero-progress interval filled exactly 512 descriptors and
  27,472 B, causing QueuePressure. Post-close zero/zero time distorted the
  aggregate rate. The retained evidence therefore proves a lower-path stall
  plus an undersized descriptor/byte envelope, not a continuous application
  deficit.
- Decision: supersede D-022's "do not enlarge" conclusion. Keep the single
  canonical publisher and independent nonblocking sinks, but freeze the final
  Wi-Fi path as: direct WHD AP-only (`ap_sta_concur=false`), one checked raw
  `TCPSocket` server/worker, 1,024-byte batching, separate admission priority
  and latency class, 1,280 descriptors, 65,520 byte arena, 2,112 byte critical
  reserve, 96% pre-full isolation, and 5 second zero-progress close.
- Wire decision: publish one lossless `CAN_RX_SEGMENT` schema 2 using a 40-byte
  header and 20-byte delta entries, maximum 23 frames/511 typed bytes. Merge
  CAN source queues by global capture sequence. Do not dual-publish legacy and
  compact records; update official Android/Windows/PC consumers together while
  retaining legacy capture decode.
- Memory decision: place only raw Wi-Fi queue storage in M7 DTCM NOLOAD;
  cursors/atomics/socket/DMA state remain normally initialized. Cap each M7 CAN
  source queue at 512. Pair M4/M7 linker ownership so M4 D2 ends at physical
  `0x30040000` and M7 network D2 owns the final 32 KiB.
- Rejected: the 32,768-byte lwIP heap candidate still overflows D2 and is not a
  product artifact. Re-enabling M7 D-cache is forbidden without a complete
  MPU/cache-maintenance platform fork because the Portenta WHD SDIO path uses
  synchronous DMA buffers.
- Gate: source-backed calculation must pass first. Then build/link-map, AP-only
  raw/product throughput, queue conservation, 2,000 fps, optional 4,000 fps,
  blocked-client/reconnect, simultaneous RC+CAN+USB+Wi-Fi, and soak are recorded
  in one table. A build or calculation is never reported as a physical pass.

## D-026 Keep the external AP-only product, restore the validated WHD compatibility role

- Date: 2026-07-27
- Status: Approved; supersedes only D-025's `ap_sta_concur=false` choice.
- Evidence: after a clean boot, the direct WHD `ap_sta_concur=false` product
  accepted a Windows TCP consumer but advanced only 536 bytes before permanent
  `WOULD_BLOCK`; the bounded policy closed it after 5 seconds. Feeder and CSM
  simultaneously preserved 40,000 injected CAN frames with zero CAN/USB drop,
  so this was isolated to the Wi-Fi lower path. Earlier same-board
  `ap_sta_concur=true` raw runs sustained 65,083--69,413 B/s.
- Decision: keep `WhdSoftAPInterface` and checked raw `TCPSocket` ownership,
  but start WHD with `ap_sta_concur=true`. The product still advertises and
  uses only its local AP; the STA role is an internal Portenta compatibility
  requirement, not a second product transport.
- Gate: rebuild/upload the paired artifacts and repeat idle, 2,000 fps,
  blocked-client, reconnect, and soak measurements. Do not infer a pass from
  the prior raw result.

## D-027 Reject synthetic throughput as product evidence and hold the Wi-Fi release

- Date: 2026-07-27
- Status: Core architecture accepted; internal Wi-Fi release gate failed.
- Artifact: M7 source manifest
  `341f948e99256a0cb07fd6883f64575c9a76b4038c11bb238ba1350fb14eec2e`,
  firmware SHA-256
  `20A84878DEEF7541726F95494550B7F8F3B90E15041474E8A1C816E974507281`.
- Evidence: after a clean upload and AP reconnect, a 15.059 s idle product
  capture accepted a PC client and delivered 2,954 bytes. It then recorded 469
  `WOULD_BLOCK` sends, zero additional socket progress, and one deterministic
  5 second `TransmitNoProgress` close. The gate failed before a high-load
  condition.
- Isolation result: the same boot received 945 CAN frames, with CAN drop,
  FIFO overflow, USB overflow, bad CRC, typed gap, segment gap, and capture gap
  all zero. Maximum main-loop gap was 3,311 us. Thus the canonical producer,
  feeder/CAN ingest, USB truth path, and RTOS worker isolation remain valid;
  the failing boundary is below `TCPSocket::send` in the WHD/lwIP path.
- Decision: retain compact schema 2, bounded queues, independent sink epochs,
  coherent diagnostics, continuous bounded AP startup retry, and the
  nonblocking worker as an instrumented candidate. Do not label the internal
  Wi-Fi transport release-ready and do not spend more RAM on its queue.
- Review blockers retained for the next implementation turn: queue-pressure
  completion must not clear producer isolation from a stale connected
  snapshot; partial WHD startup must always roll back before retry;
  `TRANSPORT_DIAGNOSTIC` needs one coherent snapshot revision; latency-bound
  records need a real queue-delay contract rather than only an early pump.
- Rejected: promoting the 65--69 kB/s synthetic raw benchmark, hiding the loss
  with a larger queue, or running the 2,000 fps Wi-Fi gate after the idle
  prerequisite failed.
- Next gate: either reproduce and pin a framework source build that fits the
  measured D2 linker budget and passes idle/nominal/2,000 fps, or qualify an
  external transport. A framework experiment is not promoted unless its
  binary/source identity and the full product stream are recorded together.

## D-028 Promote a bounded retained journal and pinned D3 network profile

- Date: 2026-07-28
- Status: Superseded by D-030. This block is historical evidence only; its
  APP ACK, retained journal, rewind/replay, and outage-retention policy is not
  an active product contract.
- Decision: preserve one canonical publication and independent USB/Wi-Fi
  sinks, but replace Wi-Fi send-and-discard storage with a 1,024-descriptor,
  65,520-byte retained SPSC journal. Socket progress advances only the send
  cursor. Record `21 APP_RX_COMMIT_ACK`, issued only after Android durable
  ordered capture, advances the reclaim cursor. Disconnect rewinds unacked
  records.
- Failure boundary: the journal never overwrites. First admission failure
  fixes `first_not_admitted_publish_seq`, latches integrity loss, emits
  `BOARD_EVENT 52`, and isolates the epoch. Record `22
  LINK_RELIABILITY_DIAGNOSTIC` makes the offered/admitted/sent/reclaimed/
  retained conservation equation and ACK/replay state visible.
- Network profile: pin ArduinoCore-mbed `6816d442...`, Mbed OS
  `17dc3dc2...`, and deterministic archive SHA-256
  `032494298FC6CAFAAD23277B8CBEB01F1BA75CA7F72CCD90383A850EE561FD70`.
  Use MSS 1460, send buffer 11,680 B, receive window 5,840 B, 40 TCP segments,
  40,960 B lwIP heap, 4,096 B TCP/IP stack, and WHD TX `PBUF_RAM`.
- Memory decision: move only the pinned lwIP heap to a link-checked D3
  envelope and install a non-cacheable/shareable MPU override before Wi-Fi.
  Keep network DMA sections in the M7-only D2 tail and the journal in DTCM.
  Limit this linker/archive overlay to the feeder product environment so
  legacy/diagnostic builds retain their standard memory map.
- Calculation: 2,000 fps product load is 57,735 B/s. The 63,408 B normal
  journal covers the approved 1.02 s outage (58,890 B). 4,000 fps remains an
  explicit transport gate at 102,172 B/s.
- Evidence: product M7, legacy MCP M7, M4 and RP2040 feeder builds pass; 11
  native contracts plus Wi-Fi/RC/control guards and the product envelope pass.
  Hardware upload, AP throughput, Android ACK/replay, simultaneous
  RC+dual-CAN+USB+Wi-Fi and soak were not run in this decision.

## D-029 Retain reset evidence without disabling product Wi-Fi

- Date: 2026-07-28
- Status: Approved; supersedes D-011's automatic Wi-Fi startup quarantine and
  one-shot retry policy. Retained boot/reset/call evidence remains active.
- Evidence correction: the observed incomplete Wi-Fi call belonged to the
  approximately 1.18 second intermediate boot between paired M4 and M7 DFU
  uploads. It does not prove a spontaneous reset, watchdog expiry, or Wi-Fi
  causal fault. The policy could not distinguish upload/user resets because
  the application reset cause was unknown.
- Decision: early-reset counts and retained call latches are diagnostic-only.
  They never force the product Wi-Fi mode Off. Slow/blocked clients and socket
  failures remain isolated at the bounded Wi-Fi sink epoch; RC, CAN ingest,
  USB, and other sinks continue independently.
- Compatibility: retained layout, historical counters, event IDs, and
  BOARD_HEALTH offsets remain stable. Legacy persisted quarantine/retry bits
  are cleared at boot and reported inactive.

## D-030 Restore a live-first observer link and demote internal Wi-Fi to candidate

- Date: 2026-07-30
- Status: Approved architecture; implementation, device HIL, simultaneous
  load, fault injection, and soak remain open. Internal Portenta Wi-Fi is
  release-blocked.
- Supersedes: D-028 in full. D-029 retained boot/reset/call evidence remains
  active because it is diagnostic black-box state, not network telemetry
  replay.
- Product requirement: control uses freshness/deadline/failsafe, observation
  prioritizes current Live with explicit loss, and Android Capture is
  independent storage. Android file durability must never control CSM RAM,
  socket lifetime, Live admission, RC, CAN, or USB.
- Data plane: keep one canonical publication and independent USB/Wi-Fi sinks.
  Replace the 1,024/65,520 retained journal with a 128-record/8,192-byte
  nonblocking live FIFO. A positive socket send immediately releases accepted
  bytes; a completed record is released and a partial write retains only its
  suffix.
- Recovery: remove application ACK, reclaim cursor, disconnect rewind, network
  backlog replay, and immutable first-boot anchor replay. Queue overflow or
  socket stall closes only that Wi-Fi epoch, records the exact drop/close
  boundary, flushes the FIFO and partial frame, and allows RC/CAN/publisher/USB
  to continue. The next TCP client starts with a fresh current
  `STREAM_SESSION` followed only by current Live.
- Startup: safe/inhibit, reset/watchdog evidence, M4 RC freshness, M7
  authority/safety, CAN ingest/TX, and USB are initialized before Wi-Fi. Wi-Fi
  start failure cannot block the control product.
- Wire compatibility: record `21 APP_RX_COMMIT_ACK` remains a reserved legacy
  ID and is decode-ignore only; it grants no permission and changes no state.
  Record `22 LINK_RELIABILITY_DIAGNOSTIC` schema 1 remains decode-only for old
  captures/tools and is not current product publication. IDs and legacy schema
  meanings are not reused.
- Network platform: the pinned Mbed/lwIP/D3/MPU/WHD profile remains a
  reproducible internal-Wi-Fi experiment, not product qualification. Its
  necessity and RAM/regression cost are re-evaluated after the smaller live
  FIFO is implemented. If common-cause stalls or required mixed-load margin
  remain, qualify an external communications MCU/gateway instead of adding
  backlog state to the control MCU.
- Qualification: host guards must prove APP ACK/replay/rewind absence and the
  128/8,192 bounds. Physical gates must cover Wi-Fi startup failure, overflow,
  stall, reconnect, fresh-session/no-backlog behavior, Android Capture
  failures, 2,000 fps + RC + dual CAN + USB + Wi-Fi, and 1/8/24-hour soak.
  Build, PC surrogate, or short idle success alone cannot promote release
  status.

## D-031 Size the live FIFO from a declared transient and close only on real loss

- Date: 2026-08-03
- Status: Approved source candidate; host/build verified, device/HIL open.
  Supersedes D-030 only for FIFO dimensions, pressure semantics and timeout
  values. Live-only transport, no APP ACK, no network replay, independent
  sinks and Android-local Capture remain unchanged.
- Corrected fact: the prior 59% policy closed at about 4,834 B after a single
  no-progress result. The observed 5,093 B high-water therefore proved the
  policy trigger, not the Mbed/lwIP/WHD sustainable ceiling. Repeated tests of
  that policy could not qualify or reject the lower transport.
- Envelope: use 256 x 16-byte descriptors and a 49,152-byte DTCM byte arena.
  Reserve four descriptors and 2,112 bytes for critical evidence, leaving
  252 records and 47,040 bytes for normal admission. At 135,000 B/s and
  686 records/s, 250 ms plus one maximum record and one 5 ms fallback requires
  34,948 bytes and 177 records. Both dimensions fit by construction.
- Pressure: enter diagnostic pressure at 32,768 bytes or 192 records and
  recover only when both occupancy values are at or below 8,192 bytes and
  64 records. Pressure may accelerate drain/bypass batching and is observable;
  it never closes a TCP epoch.
- Loss boundary: the first actual reserve/full rejection deactivates the live
  epoch, fixes the exact loss range, closes once and aborts accepted unsent
  bytes. Numeric close reason 6 remains wire-compatible but now denotes actual
  admission loss. Five seconds without positive send progress closes as reason
  4; a five-second opaque vendor call can be quarantined logically but cannot
  be cancelled inside the same MCU.
- Memory: Wi-Fi queue storage is exactly 53,248 B in CPU-only DTCM. Current
  calculation leaves 77,152 B and the product linker enforces at least
  65,536 B. D2 network DMA ownership and the 41,984 B D3 lwIP envelope do not
  move in this decision.
- Proof boundary: source calculation, static assertions, linker assertions,
  native contracts, architecture guard and product build are sufficient for
  allocation/ownership claims. Sustained socket rate, queue residence,
  reconnect behavior, RC/CAN/USB coexistence and resets require the exact
  artifact on PC and Android under aggregate 4,000 fps, fault injection and
  soak. Queue growth under that gate is a throughput failure, not a reason to
  enlarge RAM again.

## D-032 Give USB the same declared transient boundary and close the PC dual-sink gate

- Date: 2026-08-03
- Status: Approved and implemented; exact build/upload and PC I1b passed.
  Android/RC/CAN-TX I2, 120/135 kB/s capacity, fault injection and soak remain
  release blockers.
- Evidence correction: the first 60 s dual-CAN run proved source/canonical/Wi-Fi
  integrity but USB missed 69 canonical records. `usb_overflow` and
  `serial_enqueue_fail` increased by exactly 69 while Wi-Fi contained every
  injected PCAN/Kvaser sequence. The same run's 97,590 B/s Wi-Fi failure was a
  harness error caused by including the 5 s read-tail in the rate denominator.
- Decision: replace USB's eight max-frame slots with the generic byte-ring queue,
  `192 descriptors / 40,960 bytes`. At 135,000 B/s and 686 records/s, 250 ms plus
  one maximum frame requires 34,273 B/173 records. The 44,032 B queue storage
  remains in D1; the exact build leaves a 311,816 B heap span.
- Harness: throughput uses complete transport diagnostics inside the injector
  active interval. The tail remains for drain/conservation only. PCAN/Kvaser
  bus identity comes from injected source plus `CAPABILITY`, not adapter brand.
- Proof: source manifest
  `a00811b5bf2c83c61814d7d0312aecae8d03cfc012b24dc3112a4b9401cd8f2a`,
  firmware SHA-256
  `0C5AC5EB072B61299850D00E420A38857602A871ABF8FFF6FF5536B26526FE8E`.
  The final rerun captured PCAN 120,000 and Kvaser 120,000 exactly on USB and Wi-Fi;
  all CRC/sequence/source/CAN/FIFO/pool/sink loss and close counters were zero.
  Boot-cumulative USB high-water was 519 B; Wi-Fi active accepted/drain was
  100,190/100,207 B/s with 981 B net queue reduction.
- Segment correction: capture order spans independent timestamp domains, so a
  segment uses its minimum timestamp as the compact delta base and never sorts
  entries. Native regression tests and exact HIL preserved source/capture order
  while packing 241,627 frames into 10,673 segments (22.64 average).
- Harness correction: USB is opened and cleared before Wi-Fi/session creation.
  This prevents the test itself from discarding the required USB `CAPABILITY`
  anchor; the corrected final run passed that identity gate.
- Evidence hardening: each injector must reach at least 99.9% of
  `rate * duration` with no adapter/sync errors. The measured Wi-Fi epoch must
  start with one `STREAM_SESSION`, keep one boot identity and one transport
  epoch, and publish no current record 21/22. The final run reached both exact
  120,000-frame targets and passed every strengthened predicate.
- Evidence-counter correction: a partial USB batch can complete earlier
  records. Completion count/watermark is now committed before the short-write
  branch and is covered by a deterministic cross-record test.
- Linker correction: the 64 KiB DTCM reserve assertion now checks the final
  `__csm_dtcm_bss_end__`, not only the Wi-Fi queue end, so a future explicit
  DTCM arena cannot silently consume the declared reserve.

## D-033 Close control products by evidence, not compile-time permission

- Date: 2026-08-05
- Status: Active except its proposed external ArmKey/CAN-TX-gate requirements,
  which are superseded by D-034, and its common semantic Service/HIL execution
  path, which is superseded by D-035. D-029~D-032 transport and reset evidence
  decisions remain unchanged.
- Baseline audit: CSM `4583856`, feeder `1dfbdb3`, Android `bd31e75` were checked
  against the owner closeout input. Confirmed source defects are RC-loss
  steering neutral omission, MDPS-only drive leakage, compile-time autonomy
  release, Service/HIL direct raw-CAN bypass, missing artifact pairing and CSM feeder-UART ISR/main
  handoff. External hardware/credential facts are not synthesized in software.
- Profile decision: keep `BENCH_005_007_V1` as a named isolated adapter.
  `MdpsBench` maps only `0x007`. `RemoteProduct` cannot advertise control ready
  without a runtime autonomy provider and approved vehicle model pack.
  `ServiceHil` remains an explicitly identified engineering profile and a
  release-capable variant requires authenticated client evidence.
- Common control path: RC, ServiceHil and future autonomy are source adapters.
  They provide semantic latest values/events to one M7 coordinator. Authority,
  safety, absolute 5/20 ms release, limiter, reversal, profile mapper,
  `FdcanOwner` and TX completion journal are never bypassed by raw host bytes.
- Failure decision: when the CAN backend can still transmit, a valid RC
  stale/release transition schedules both drive and steering neutral. When a
  hard gate, bus-off or backend fault prevents physical TX, firmware records
  inhibit/failure evidence and does not claim neutral was sent.
- Artifact decision: M7, M4 and feeder expose compatible protocol, contract,
  profile and build-bundle identities. Missing/mismatched identities keep
  sources not-ready and authority inhibited. Build/upload tools produce one
  manifest rather than relying on filenames.
- Feeder correction: RP2040 core0/core1 uses atomic SPSC/mailbox ownership and
  is not the identified race. CSM `FeederUartIngress` currently shares plain
  restart/stat fields between DMA error ISR and main. ISR will publish only an
  atomic error event; main owns restart and accounting.
- CAN DB boundary: HNO1 Rev 0 input SHA-256
  `EEB0AE6DB9D30EB4EFB6229CA61893AE5E59CDEF9ABCE0C175701067073D16A1`
  defines Driving 1 Mbit/s and System 500 kbit/s but conflicts with bench
  `0x005/0x007` and has unresolved signal metadata. It cannot be a CSM control
  mapper until an approved generated vehicle-contract package exists.
- Verification: ownership and state behavior close in native guards/builds;
  safety-input behavior, credential provisioning, CAN timing/ACK, reset/fault
  coexistence and soak remain explicit HIL/release gates.

## D-034 Remove unimplemented external control interlocks

- Date: 2026-08-07
- Status: Active; supersedes every D-033 requirement for D1 `CanTxEnable`, D3
  `ArmKey`, hardware-gate readback and their compile-time substitutes.
- Operator outcome: Service/HIL ARM requires no undocumented switch or wiring.
  It remains gated by explicit operator intent, current epoch/boot identity,
  compatible capability, fresh health, RC/autonomy priority, heartbeat/lease,
  E-stop/field-power/encoder fault state and an operational CAN backend.
- Cleanup: pin ownership, safety state, gateway input/decision, profile guards,
  tests and active hardware documentation for the removed interlocks are deleted.
  A source guard fails if those names return.

## D-035 Make Service/HIL Host CAN a mechanical raw I/O boundary

- Date: 2026-08-18
- Status: Active for the semantic/raw ownership split; its direct one-shot/no-FIFO
  execution detail is superseded by D-036.
- Boundary: upper VSM/control software owns vehicle command meaning, sequence,
  count, ramp, CENTER, EHB and explicit neutral. CSM owns session/authority/lease/
  hard-safety admission, static bus/standard-ID/DLC/RTR policy, FDCAN admission
  and actual HW evidence. RC/autonomy semantic limiter/mapper remains on M7.
- Execution: N physical frames are N individual `HostCanTxRequest` records. Each
  allowed payload is byte-preserved and submitted once through the existing
  `BuiltinCanTxOwner`. No latest overwrite, repeat/count generation, implicit
  retry, persistent Host FIFO, cadence scheduler or TX segment is introduced.
- Evidence: ACK Accepted follows tracked HW FIFO admission. Each Accepted request
  reaches one terminal record 23; transmitted success additionally requires
  matching `CAN_TX_RAW`. Busy/journal-full/pre-HW failure is explicit rejection,
  and nonterminal pending is not a terminal failure.
- Failure: already-HW-owned requests are not silently flushed on session loss;
  new Host ARM waits for terminal closure. Lease/safety closure rejects new work.
  A vehicle watchdog or independent hard-safety on message cessation is a release
  gate because CSM no longer generates Host stale-neutral frames.
- Verification: payload identity, N attempt accounting, owner-full/reject/pending/
  terminal behavior, old-session closure, RC regression, canonical consumers,
  target build and combined external-analyzer HIL.

## D-036 Add bounded opaque Host cadence lanes

- Date: 2026-08-18
- Status: Superseded by D-037. Retained only as the rejected historical design;
  upper vehicle semantics and N-request identity remain valid through D-035/D-037.
- Verified defect: Android/TCP/main-loop timing reached direct Host FDCAN admission;
  the 40-byte parser budget split coalesced control records across variable loops,
  and all three IDs phase-locked at ARM.
- Execution: add three static opaque lanes, each capacity 8: `0x005` at 5000 us,
  `0x007` at 20000 us and `0x364` at 20000 us with 5000 us first/restart phase.
  Same ID is FIFO, different lanes progress independently, and one lane has at
  most one HW in-flight request. No payload decode, latest overwrite, generated
  frame, semantic retry, TX segment, heap allocation or second FDCAN owner exists.
- Admission/evidence: ACK Accepted means bounded SW-queue admission. Full rejects
  newest without mutation. Each accepted command reaches terminal record 23;
  transmitted truth also needs matching `CAN_TX_RAW`. Nonterminal pending keeps
  correlation and late completion never creates a catch-up burst.
- Failure: session/authority/lease/safety loss flushes only not-yet-HW pending
  frames. HW-owned work stays journaled until terminal, and fresh ARM waits all
  old owner-origin slots before resetting the Host queue epoch.
- Origin exclusivity: while an admitted Host lease is active, RC parsing and
  telemetry continue but `RemoteControlRuntime` emits neither RemoteControl nor
  SafetyNeutral CAN frames. RC qualification must not be used as an extra
  condition for this silence; otherwise identical 0x005/0x007 origin schedules
  overlap and violate physical spacing.
- Verification: FIFO/bytes/spacing/phase/fairness/in-flight/pending/full/flush/
  wrap/parser native contracts, control guard, target build, external Kvaser
  cadence and long nominal ARM/soak. Source/build success is not physical proof.

## D-037 Rebase Host raw execution onto the actual FDCAN HW FIFO

- Date: 2026-08-19
- Status: Implemented candidate; target build/device/four-window external Kvaser
  evidence determines release. Supersedes D-036 Host execution policy while
  preserving D-035's semantic/raw boundary, parser coalescing fix and Host-lease
  RC/SafetyNeutral silence.
- Defect: equal-rate producer/consumer queues plus completion-observation release
  rebasing created a non-draining Host backlog and converted software/evidence
  delay into physical cadence drift. A 24-frame actuator backlog also increased
  stale traffic already admitted below the application boundary.
- Execution: Android owns absolute nominal 5000/20000/20000 us request creation.
  CSM has no Host SW execution FIFO, ID lane, phase or scheduler. Each fresh,
  authorized, statically valid raw record is byte-preserved and submitted once in
  parser order to `BuiltinCanTxOwner`; current HW/journal busy/full is an explicit
  reject with no retention/retry. ACK Accepted follows only tracked admission to
  the real 3-element Mbed FDCAN TX FIFO.
- Freshness/safety: two coherent heartbeat samples qualify the transport epoch.
  Sender-time age/future/replay is rejected with reason 26; excessive heartbeat
  timeline divergence latches until a new epoch. DISARM/epoch/Host-authority loss
  requests Host-origin HW cancellation, hard safety requests all-origin
  cancellation, and owner slots remain until hardware terminal truth.
- Evidence: Host terminal record 23 and matching `CAN_TX_RAW` remain separate from
  ACK. `CAN_TX_RAW` delivery is latency-bounded; its timestamp remains completion
  observation, not proven start-of-frame time.
- Fixed product budgets: heartbeat extra-lag 100 ms, Host command max age 40 ms,
  future tolerance 20 ms. These precede HIL and must not be loosened merely to
  pass a test. External Kvaser release requires idle, active steering, endurance
  and stop windows with nominal rate/count plus median/p95/p99/max and duplicate
  catch-up gates; D-036 artifacts cannot qualify D-037.
