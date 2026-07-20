# CSM 펌웨어 아키텍처

## 코어 책임

### M4

- R16SM UART byte 수신
- CRSF frame bounds/type/CRC 검증
- channel 정규화, link/failsafe/stale 판정
- 고정 크기 latest-sample mailbox publish

M4는 CAN ID, 차량 payload, authority, safety, CAN driver를 알지 않는다.

### M7

- M4 mailbox 검증과 source 변환
- upstream autonomy 관측
- 단일 authority 및 safety state owner
- command limiting과 vehicle mapping
- 유일한 local CAN TX gateway
- CAN/safety/authority evidence 생성
- canonical uplink publisher와 sink service

## 제어 데이터 흐름

```text
R16SM -> M4 UART/parser/normalizer
      -> fixed latest-sample mailbox
      -> M7 RemoteControlSource
      -> AuthorityManager
      -> SafetySupervisor
      -> CommandLimiter
      -> VehicleCommandMapper
      -> CanTxGateway
      -> CAN backend
      -> CAN_TX_RAW evidence
```

`AuthorityManager`와 mapper는 CAN driver를 호출하지 않는다. 최종 write는 `CanTxGateway` 하나만 소유한다.

## 관측 데이터 흐름

```text
CAN/RC/authority/safety evidence sources
  -> bounded priority admission
  -> CanonicalPublisher
  -> immutable encoded frame
  -> UsbCdcSink
  -> WifiTcpSink
```

관측 pipeline은 제어 pipeline의 결과를 읽을 수 있지만 제어 state를 직접 변경하지 않는다.

## 실행 및 메모리 원칙

- hot path는 고정 크기 storage와 bounded queue를 사용한다.
- producer는 transport write를 기다리지 않는다.
- consumer별 진행 상태와 실패 상태를 분리한다.
- `main.cpp`는 module construction, profile wiring, deterministic service 호출만 담당한다.
- blocking network 호출, parser body, queue policy, authority policy를 `main.cpp`에 넣지 않는다.

## 현재와 목표의 경계

현재 remote/authority/control은 deny-first skeleton이고 실제 M4-M7 IPC와 production CAN TX에 연결되지 않았다. 기존 passive profile은 회귀 기준으로 유지한다. Production Remote Observer는 별도 build profile과 HIL gate를 통과한 뒤에만 product artifact가 된다.
