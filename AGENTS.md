# AGENTS.md

이 저장소는 무선 리모컨, Windows VSM USB CDC 관측, VSM Android Wi-Fi 관측을 동시에 제공하는 CSM 펌웨어의 정식 작업 공간이다. 실제 펌웨어는 `firmware/csm`에 있으며, VSM 앱 코드는 각 저장소에서 관리한다.

## 진입 순서

1. `BRIEF.md`
2. 요청과 정확히 일치하는 권위 문서 하나
3. 범위가 불명확하거나 두 계약을 함께 바꿀 때만 `INDEX.md`

모든 Markdown과 하위 폴더를 일괄 읽지 않는다. 이 파일 외에 별도 `AGENTS.md` 계층을 만들지 않는다.

## 권위와 원본

- 제품 정체성: `docs/product/PRODUCT_DEFINITION_KO.md`
- 실사용 시나리오: `docs/product/OPERATING_SCENARIOS_KO.md`
- 펌웨어 경계: `docs/architecture/FIRMWARE_ARCHITECTURE_KO.md`
- uplink/fanout: `docs/architecture/UPLINK_TRANSPORT_ARCHITECTURE_KO.md`
- typed wire: `firmware/csm/shared/docs/TRANSPORT_AND_RECORDS_KO.md`
- 검증 등급: `docs/quality/VERIFICATION_POLICY_KO.md`
- 현재 상태: `BRIEF.md`
- 결정과 폐기 근거: `history/decisions/DECISION_LEDGER_KO.md`

하드웨어 문서는 실측·배선·bring-up 근거이며 제품 또는 wire 계약을 재정의하지 않는다.

## 절대 규칙

- 우선순위는 hard safety, upstream autonomy, RC remote, service host, monitoring 순이다.
- M4는 RC 수신·파싱·정규화만 담당하고 M7만 공통 권한·hard-safety·CAN admission과
  physical CAN TX를 소유한다. RC/autonomy semantic mapping은 M7에 남는다. 명시적
  Service/HIL Host raw profile에서는 upper control SW가 vehicle meaning/sequence와
  nominal cadence/N개 request 생성을 소유한다. CSM은 fresh/authorized payload를
  byte-preserve해 sole owner의 실제 3-slot FDCAN FIFO에 즉시 한 번만 admission한다.
  busy/full은 현재 request를 reject하며 Host SW 실행 FIFO나 cadence scheduler가 없다.
- Production VSM은 observer-only다. host 제어는 명시적인 Service/HIL profile에서만 허용한다.
- RC/권한/CAN 수신 hot path는 USB·Wi-Fi telemetry보다 항상 우선한다.
- typed record는 canonical publisher에서 한 번 순서화·직렬화한 뒤 USB와 Wi-Fi의 독립 bounded sink로 fanout한다.
- 느리거나 끊긴 sink는 RC, CAN, publisher, 다른 sink를 막거나 공유 버퍼를 무기한 점유할 수 없다.
- sink별 queue, drop, high-water, epoch, close reason을 숨기지 않는다.
- `CONTROL_ACK`는 요청 판정 증거다. matching `CAN_TX_RAW` 없이는 실제 CAN 송신 성공으로 표시하지 않는다.
- heap 기반 hot-path 할당, 무제한 backlog, 조용한 손실, 암묵적 재전송을 금지한다.

## 구현 규칙

- 코드 전에 최종 owner, 입력·출력 type, queue 한계, 실패 동작, 검증 gate를 확정한다.
- `main.cpp`는 생성·wiring·상위 loop 조정만 소유한다. 임시 기능을 몰아넣지 않는다.
- production 경로에 나중에 분리할 구조를 넣지 않는다.
- 설계 변경 시 구형 code path, flag, test, 문서, build env를 검색해 삭제하거나 공존 근거를 decision ledger에 남긴다.
- wire 변경은 canonical wire 문서와 소비자 호환 fixture를 같은 변경에서 다룬다.
- build, upload, bench, HIL, 차량 성공은 실제 수행한 범위만 주장한다.

## 작업 라우팅

- 제품 방향·사용 시나리오: product 및 decision ledger
- M4/M7, authority, CAN TX: firmware architecture
- USB/Wi-Fi, publisher, queue, backpressure: uplink architecture
- record ID·payload·CRC: canonical typed wire
- 빌드·PC 설정: development setup
- 부하·동시 운용·release: verification policy

## 결과 보고

- 변경·삭제 파일
- 데이터 흐름과 경계 변화
- 실행한 guard/build/test
- 실제 보드·RC·USB·Wi-Fi·HIL 확인 여부
- 남은 field risk와 다음 gate
