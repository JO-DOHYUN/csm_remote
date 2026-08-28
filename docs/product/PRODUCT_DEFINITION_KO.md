# CSM Remote Product Definition

Authority: `PRODUCT PURPOSE / PROFILES / OPERATOR GUARANTEES`

제품 불변식은 `PRODUCT_CONSTITUTION_KO.md`, 현재 구현은
`../architecture/ACTIVE_ARCHITECTURE.yaml`과 L2 architecture 문서가 소유한다.

## 제품 목적

CSM Remote는 RC와 승인된 service host의 제어 입력을 권한과 admission 경계에서
판정해 차량의 단일 물리 CAN 실행 경계로 전달한다. RC가 Host보다 높은 우선순위를
가지며 현재 active profile에는 autonomy source가 없다. 동시에 차량 수신,
제어 admission, terminal outcome, 실제 실행, board/transport 상태를 canonical typed
evidence로 만들어 독립 observer에 제공한다.

## 제품 보장

- 안전과 권한 판정이 관측·저장·표시 부하보다 우선한다.
- 한 관측 sink의 지연·단절·overflow가 control 또는 다른 sink를 막지 않는다.
- 명령 수락, 최종 outcome, 실제 물리 실행을 서로 바꾸어 표시하지 않는다.
- loss, drop, reset, reconnect, session과 failure reason을 숨기지 않는다.
- bounded resource가 가득 차면 현재 작업을 명시적으로 거절하거나 해당 관측 epoch를
  명시적으로 degraded/closed 처리한다.
- build나 연결 상태만으로 vehicle/HIL 성공을 주장하지 않는다.

## Operating Profiles

### Production

- VSM은 observer-only다.
- 제어원 우선순위와 admission이 항상 monitoring보다 앞선다.
- canonical evidence는 USB와 Wi-Fi observer가 독립적으로 소비할 수 있다.

### Service/HIL

- 명시적으로 식별된 engineering profile에서만 host control을 허용한다.
- service host는 차량 의미와 실행 시퀀스를 소유하고 CSM은 권한, safety, 정적 frame
  admission, 물리 실행과 hardware truth를 소유한다.
- host disconnect, lease/stale, authority 상실과 CAN failure는 새 작업을
  fail closed하며 이미 수락된 작업의 terminal truth를 조용히 지우지 않는다.

## Evidence Contract

canonical typed wire 정본은
`../../firmware/csm/shared/docs/TRANSPORT_AND_RECORDS_KO.md`다. Observer는 연결,
admission, terminal outcome, physical execution, capture/storage 상태를 구분한다.

## 제품 외 범위

- 현 architecture, processor/core 배치와 구체 scheduler/queue/FIFO 정책
- 승인되지 않은 vehicle semantic mapping
- 검증되지 않은 transport나 hardware capability
- 무기한 CSM network backlog 또는 reconnect replay

현재 구현 세부는 제품 불변식이 아니며 `ARCH_CHANGE` 절차를 통해 더 나은 구조로
교체할 수 있다.
