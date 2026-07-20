# CSM Remote 제품 정의

## 제품 정체성

CSM Remote는 Portenta H7의 M4 RC frontend와 M7 단일 권한·안전·CAN gateway를 사용하고, 동일한 차량 evidence를 Windows VSM과 Android VSM에 동시에 제공하는 산업용 CSM 플랫폼이다.

제품의 우선순위는 다음으로 고정한다.

```text
hard safety > upstream autonomy > RC remote > service host > monitoring
```

## 최종 제품 형태

- Radiolink T16D/R16SM 입력은 M4가 수신·검증·정규화한다.
- M7만 authority, safety, limiter, vehicle mapping, CAN TX를 소유한다.
- Windows VSM은 USB CDC typed stream을 관측한다.
- VSM Android는 Wi-Fi TCP typed stream을 관측한다.
- USB와 Wi-Fi는 동일한 canonical record order와 identity를 소비하는 독립 sink다.
- RC는 VSM 연결·혼잡·재접속 여부와 무관하게 우선 동작해야 한다.

## 제품 profile

### Passive Observer Baseline

기존 안전 회귀 기준이다. 두 CAN bus를 관측하고 USB CDC로 evidence를 내보내며 host downlink와 local CAN TX를 compile-time 차단한다.

### Production Remote Observer

최종 양산 목표다. RC는 M7 authority를 통해 제한적으로 차량 제어할 수 있고, Windows USB와 Android Wi-Fi는 observer-only다. 이 profile은 아직 구현·HIL 완료 상태가 아니다.

### Full Instrumented Service/HIL

bench 전용이다. 명시적 service host 동작과 진단 기능을 허용할 수 있으나 production artifact와 build identity를 분리한다.

## 제어 안전 계약

- upstream autonomy release가 명확하지 않으면 local motion CAN TX는 0이다.
- unknown, active, recently active, ambiguous, protocol fault는 모두 unsafe다.
- M4, VSM, parser, UI는 최종 CAN TX 권한을 가질 수 없다.
- 모든 local motion TX는 M7 `CanTxGateway`와 hardware gate를 통과해야 한다.
- `CONTROL_ACK`는 요청 수락/거부 증거이며 실제 송신 성공은 matching `CAN_TX_RAW`로만 증명한다.

## 관측 데이터 계약

- typed 원본 record가 truth이며 UI projection은 truth가 아니다.
- `CAN_RX_RAW`, `CAN_RX_SEGMENT`, `CONTROL_ACK`, `CAN_TX_RAW`, `BOARD_EVENT`, `BOARD_HEALTH`, `CAPABILITY`는 서로 다른 evidence다.
- sink 손실, queue overflow, reconnect epoch, parser/storage 실패를 숨기지 않는다.
- CSM은 Wi-Fi 장기 backlog나 history server가 아니다. Android의 durable capture는 앱 책임이다.

## 비목표

- Wi-Fi를 RC 제어 경로로 사용하지 않는다.
- Android 전용 임의 protocol을 만들지 않는다.
- USB 장애를 Wi-Fi가 기다리거나 Wi-Fi 장애를 USB가 기다리지 않는다.
- production VSM에 raw vehicle control affordance를 제공하지 않는다.
- 검증되지 않은 tablet USB/CAN 또는 특정 양산 tablet port를 제품 전제로 고정하지 않는다.
