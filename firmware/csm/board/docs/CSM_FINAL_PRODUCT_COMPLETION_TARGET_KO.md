# CSM Final Product Completion Target

## 1. Product Purpose

CSM은 차량 2-bus CAN을 관찰하고 VSM에 typed evidence를 올리는
Passive CAN front-end다. 제품 성공 기준은 "프레임을 많이 받는 것"이 아니라
USB 연결/해제, CDC session open/close, MCU reset/boot 중 차량 CAN에
host-originated traffic이나 비정상 물리 영향을 주지 않는 것이다.

현재 Portenta H7 M7 + Mid Carrier + MCP2515/TJA1050 구성은
`Software Passive Prototype`이다. 하드웨어 default-safe, scope, reference
analyzer, DTC evidence가 없으면 `Verified Passive Product`가 아니다.

## 2. Product Artifact

Field/product build name:

```text
portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
```

이 env만 현재 차량용 제품 업로드 대상이다.

- bus0: MCP2515/TJA1050 observe-only.
- bus1: Mid Carrier J4/U2 observe-only.
- Host TX/control/downlink/test TX: compile-time removed.
- USB reconnect reset: disabled.
- ACK-observe: stable host session 이후 허용.
- Pre-session: no-ACK hold, old payload replay 금지.

Full Instrumented와 lab ACK/TX env는 bench 전용이며 실차 passive acceptance로
사용하지 않는다. 1-bus passive artifact는 제품이 아니다.

## 3. Lifecycle

```text
BOOT_SAFE
  -> CAN_FRONTEND_PRESESSION_HOLD
  -> HOST_ABSENT_NO_REPLAY
  -> USB_ATTACH_QUARANTINE
  -> CAN_FRONTEND_SESSION_READY
  -> ACK_OBSERVE
```

`USB_ATTACH_QUARANTINE`은 CDC/uplink/session payload cleanup이다. CAN
front-end drain을 멈춘다는 뜻이 아니다. 다만 현재 firmware product policy는
USB power-up/session 안정 전 CAN front-end initialization과 ACK-observe를
지연한다.

## 4. No-Replay Contract

- Host absent 동안 CAN payload를 typed record로 staging하지 않는다.
- Host absent 동안 old segment builder state를 보존하지 않는다.
- Session open 시 `host_session_epoch`와 `transport_epoch`가 증가한다.
- Session open 직후 old CAN payload replay는 0이어야 한다.
- `HOST_ABSENT_DISCARD_SUMMARY`는 이후 VSM evidence로 보고한다.

## 5. Fault Containment

- MCP listen-only/normal readback과 TXREQ readback을 주기적으로 검사한다.
- TXREQ가 passive product에서 set되면 product-blocking fault다.
- Readback violation은 latch되고, 조용히 clear하지 않는다.
- Fault 시 가능하면 no-ACK hold로 되돌려 차량 CAN에 host-originated
  traffic이 나가지 않게 한다.
- ACK capability와 host TX capability를 절대 같은 것으로 취급하지 않는다.

## 6. Evidence

VSM에 최소한 다음 evidence를 올린다.

- `CAN_FRONTEND_PRESESSION_HOLD`
- `CAN_FRONTEND_SESSION_READY`
- CDC session open/close/DTR change
- host absent discard summary
- MCP passive readback / violation
- MCP TXREQ violation
- passive violation latch
- hardware evidence claim/reference fields

Hardware evidence fields are claims, not proof. Verified passive requires
external analyzer/scope/DTC artifact validation outside the firmware.

## 7. Firmware vs Hardware Limit

Firmware can:

- set safe pins as early as `setup()` starts,
- defer CAN initialization until session stability,
- block host TX/control/downlink,
- prevent old payload replay,
- detect readback/TXREQ violations,
- publish lifecycle evidence.

Firmware cannot prove pre-boot or unpowered transceiver behavior alone. Field
SKU hardware must provide default-safe transceiver pins, TXD recessive bias,
optional TX gate, VBUS/back-power containment, and USB/CAN ground/common-mode
policy.
