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
- 상태: Active. 2026-07-22 host contract/build와 REF/A/B/C 각 180초 실보드
  1차 gate 완료; 반복·장시간 HIL 대기.
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
  shared limiter and mapper.
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
