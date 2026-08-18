# CSM Remote 제품 정의

## 제품 정체성

CSM Remote는 Portenta H7의 M4 RC frontend와 M7 단일 권한·안전·CAN gateway를 사용하고, 동일한 차량 evidence를 Windows VSM과 Android VSM에 동시에 제공하는 산업용 CSM 플랫폼이다.

제품의 우선순위는 다음으로 고정한다.

```text
hard safety > upstream autonomy > RC remote > service host > monitoring
```

## 최종 제품 형태

- Radiolink T16D/R16SM 입력은 M4가 수신·검증·정규화한다.
- M7만 공통 authority, hard-safety, CAN admission과 physical CAN TX를 소유한다.
  RC/autonomy semantic 경로의 limiter/vehicle mapping은 M7에 남고, Service/HIL
  Host raw 경로의 vehicle meaning/sequence는 상위 control SW가 소유한다.
- Windows VSM은 USB CDC typed stream을 관측한다.
- VSM Android는 Wi-Fi TCP typed stream을 관측한다.
- USB와 Wi-Fi는 동일한 canonical record order와 identity를 소비하는 독립 sink다.
- RC는 VSM 연결·혼잡·재접속 여부와 무관하게 우선 동작해야 한다.

## 제품 profile

### Passive Observer Baseline

기존 안전 회귀 기준이다. 두 CAN bus를 관측하고 USB CDC로 evidence를 내보내며 host downlink와 local CAN TX를 compile-time 차단한다.

### Production Remote Observer

최종 양산 목표다. RC는 M7 authority를 통해 제한적으로 차량 제어할 수 있고,
Windows USB와 Android Wi-Fi는 observer-only다. 현재 M4-M7 IPC와 RC 관측 경계는
구현됐지만 mapper 기본값은 `None`, local CAN TX capability는 Off이며 autonomy
입력도 제품 runtime에 아직 연결되지 않았다. 따라서 현재 artifact는 차량 제어
release가 아니다.

### Full Instrumented Service/HIL

bench 전용이다. 명시적 service host 동작과 진단 기능을 허용할 수 있으나 production artifact와 build identity를 분리한다.

## 제어 안전 계약

- upstream autonomy가 `InactiveConfirmed`로 명시적으로 release하지 않으면 local
  motion CAN TX는 0이다. unknown, active, recently active, ambiguous, protocol
  fault는 모두 fail-closed다.
- autonomy release 뒤 RC가 유효하면 service host보다 먼저 제어 경계를 예약한다.
- RC가 검출됐으나 중립 확인 전이면 local motion source를 차단한다.
- RC stale/failsafe는 즉시 중립으로 전환하고 안정된 release qualification 뒤에만 하위 source를 허가한다.
- malformed CRSF, IPC integrity failure, M4 heartbeat loss는 authority를 해제하지 않는 fail-closed 상태다.
- M4, VSM, parser, UI는 최종 CAN TX 권한을 가질 수 없다.
- 모든 local motion TX는 M7 `CanTxGateway`의 authority, safety, frame policy와 실제 CAN backend 상태를 통과해야 한다.
- Service/HIL Host raw request는 static bus/ID/DLC/RTR allowlist와 위 공통 gate를
  통과한 뒤 ID별 fixed FIFO와 mechanical release gate를 거쳐 sole
  `BuiltinCanTxOwner`에 제출한다. CSM은 허용 payload를 byte-preserve하며 차량 의미,
  count, ramp, CENTER, EHB 또는 neutral을 생성하지 않는다.
- `0x005/0x007/0x364` lane은 각각 capacity 8이며 same-ID FIFO, reject-newest-on-full,
  one-HW-in-flight를 지킨다. 다른 lane은 독립 진행하고 late completion 뒤 catch-up
  burst를 만들지 않는다.
- `CONTROL_ACK`는 요청 수락/거부 증거다. `CAN_TX_RAW`는 별도 TX evidence지만
  현재 built-in CAN의 driver FIFO enqueue 수락 직후 발행되는 경로는 물리 송신
  완료 증거가 아니다. release에서는 FDCAN TX completion/TXBTO와 상관된 record와
  외부 analyzer가 모두 일치해야 실제 송신 성공으로 판정한다.

## 관측 데이터 계약

- typed 원본 record가 truth이며 UI projection은 truth가 아니다.
- `CAN_RX_RAW`, `CAN_RX_SEGMENT`, `CONTROL_ACK`, `CAN_TX_RAW`, `BOARD_EVENT`, `BOARD_HEALTH`, `CAPABILITY`는 서로 다른 evidence다.
- sink 손실, queue overflow, reconnect epoch, parser/storage 실패를 숨기지 않는다.
- CSM은 Wi-Fi 장기 backlog나 history server가 아니다. Android의 durable capture는 앱 책임이다.
- Wi-Fi는 앱 capture commit을 기다리지 않으며 과거 telemetry나 motion
  command를 재생하지 않는다. 재접속은 현재 full publish identity의 새
  segment에서 시작한다.
- onboard Wi-Fi worker의 logical isolation은 같은 M7/kernel/WHD/SDIO/전원을
  공유하는 common-cause fault의 물리 격리를 의미하지 않는다. Wi-Fi fault가
  RC/CAN에 물리적으로 영향을 줄 수 없다는 양산 요구는 별도 통신
  MCU/gateway와 HIL 없이 승인하지 않는다.

## 비목표

- Wi-Fi를 RC 제어 경로로 사용하지 않는다. 명시적 Service/HIL Host raw path는
  별도 bench profile이다.
- Android 전용 임의 protocol을 만들지 않는다.
- USB 장애를 Wi-Fi가 기다리거나 Wi-Fi 장애를 USB가 기다리지 않는다.
- production VSM에 raw vehicle control affordance를 제공하지 않는다.
- 검증되지 않은 tablet USB/CAN 또는 특정 양산 tablet port를 제품 전제로 고정하지 않는다.

## 제어 release gate

- upstream autonomy monitor의 실제 CAN profile과 runtime wiring
- 실제 차량 vehicle mapping 승인; `0x007` MDPS mapping은 bench 전용이며 기본 Off
- completion-correlated `CAN_TX_RAW`와 Kvaser 등 외부 analyzer 대조
- RC loss/reacquire, USB+Wi-Fi+CAN 동시 부하, reset/fault injection, 장시간 soak

CSM의 상시 self-debug와 reset recovery 경계는 제품 기능으로 유지하며 상세 정책은
`docs/architecture/DEBUG_AND_RECOVERY_ARCHITECTURE_KO.md`를 따른다.
