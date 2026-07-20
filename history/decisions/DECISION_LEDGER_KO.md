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
