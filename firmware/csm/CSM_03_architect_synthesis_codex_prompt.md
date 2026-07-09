# CSM 03. 확장추론 / 최종 구상안

## 0. 문서 목적

이 문서는 HAMT2-platform CSM을 Codex가 바로 구현할 수 있도록 만들기 위한 최종 설계 기준이다. 현재 목표는 기존 Portenta H7 스케치를 부분 보수하는 것이 아니라, VSM evidence-first CAN control workstation이 신뢰할 수 있는 **typed evidence + safety-gated control gateway**로 CSM 뼈대를 확정하는 것이다.

CSM은 단순 USB-CAN 브릿지가 아니다. CSM은 다음 역할을 동시에 수행해야 한다.

- 2채널 CAN evidence publisher
- typed USB uplink/downlink endpoint
- host control request validator
- `CONTROL_ACK` accept/reject publisher
- actual CAN write success/fail auditor
- `CAN_TX_RAW` truth publisher
- `CAN_RX_RAW` bus observer
- `CAPABILITY`/`BOARD_HEALTH`/`BOARD_EVENT` board diagnostic publisher
- VSM control safety gate
- CAN bus physical/backend health observer
- HIL 검증용 firmware baseline

---

## 1. 최종 제품 정의

### 1-1. 프로젝트 이름

- HAMT2-platform CSM typed evidence + control gateway

### 1-2. 한 문장 목적성

Portenta H7 M7 + Portenta Mid Carrier ASX00055 + 외부 MCP2515/TJA1050 + J4 CAN1 구성을 VSM이 신뢰할 수 있는 typed evidence/control gateway로 만들고, VSM의 제어 의도부터 실제 CAN 송신 audit까지 원인 분리 가능한 증거 체계를 제공한다.

### 1-3. 최종 사용자 장면

사용자는 차량 CAN 개발/시험자다. 사용자는 VSM을 PC에서 실행하고 CSM 보드를 USB CDC로 연결한다. CSM은 다음을 보장해야 한다.

1. 부팅 직후 `CAPABILITY`를 송신한다.
2. 일정 주기로 `BOARD_HEALTH`와 `CAPABILITY`를 갱신한다.
3. 각 CAN bus에서 수신한 frame을 `CAN_RX_RAW`로 올린다.
4. VSM이 보낸 control request를 검증한 뒤 `CONTROL_ACK`로 수락/거절을 올린다.
5. 실제 CAN controller write가 성공한 경우에만 `CAN_TX_RAW`를 올린다.
6. 오류, drop, overflow, bus fault, host parser fault, MCP fault를 숨기지 않고 `BOARD_HEALTH`/`BOARD_EVENT`로 올린다.
7. board safety state가 안전하지 않으면 신규 control TX를 거절한다.
8. COM open만으로 board alive를 주장하지 않는다.
9. bus0/bus1 번호만으로 System/Drive 역할을 단정하지 않는다.

---

## 2. 현재 기준 입력

### 2-1. 현재 하드웨어

- MCU: Arduino Portenta H7 M7
- Carrier: Arduino Portenta Mid Carrier ASX00055
- bus0 physical backend: external MCP2515/TJA1050 module
- bus0 wiring: D7 CS, D8 COPI/SI, D9 SCK, D10 CIPO/SO, D11 INT
- bus0 power: MCP module VCC 5 V, common GND, level shifter required
- bus0 clock: MCP2515 8 MHz crystal
- bus0 CAN speed: Classic CAN 2.0 500 kbps
- bus1 physical backend: Mid Carrier J4 CAN1 through onboard U2 CAN PHY
- bus1 switch: SW2 CAN-enabled position required
- bus1 CAN speed: Classic CAN 2.0 500 kbps
- final active env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm`

### 2-2. 현재 repository/code 기준

- 기준 zip: `J_ArdP7_AM2_CSM (3).zip`
- GitHub: `https://github.com/JO-DOHYUN/HAMT2-platform.git`
- 현재 주요 파일:
  - `src/main.cpp`
  - `platformio.ini`
  - `include/BoardPins.h`
  - `include/protocol/TypedFrame.h`
  - `include/protocol/TypedRecords.h`
  - `include/board/HostDownlinkParser.h`
  - `include/board/CapabilityPublisher.h`
  - `board/BRIEF.md`
  - `docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO.md`
  - `docs/architecture/CONTROL_EVIDENCE_CONTRACT_KO.md`

### 2-3. 현재 코드에서 이미 있는 핵심

- typed frame v1
- CRC16-CCITT
- `CAN_RX_RAW`
- `CAN_TX_RAW`
- `CONTROL_ACK`
- `BOARD_EVENT`
- `BOARD_HEALTH`
- `CAPABILITY`
- `ADC_SAMPLE`
- `HOST_CAN_TX_REQUEST`
- MCP2515 8 MHz / 500 kbps 설정
- Mid Carrier J4 built-in CAN lane
- host TX allowlist `0x503`, `0x510..0x513`
- MCP2515 INT_N level-hint + polling fallback 구조
- `CONTROL_ACK` 후 `CAN_TX_RAW` audit 방향

### 2-4. 현재 코드의 주요 부족점

현재 코드는 VSM 최종 구조에 근접해 있지만, 최고 완성도 기준에서는 아래가 부족하다.

1. `src/main.cpp` 단일 파일 집중도가 높다.
2. safety state가 존재하지만 실질적 `ARMED`, lease, host heartbeat, timeout 제어가 약하다.
3. `CONTROL_ACK` reason code가 control/safety/queue/bus fault를 충분히 표현하지 못한다.
4. `BOARD_HEALTH` payload가 VSM 진단기 역할에 필요한 counters를 모두 담지 못한다.
5. `CAPABILITY` bus descriptor가 physical backend와 semantic role을 섞을 위험이 있다.
6. bus role이 build flag로 `system/drive`처럼 박히는 구조는 “어느 채널에 물려도 구별 가능” 목표와 충돌한다.
7. host downlink command 종류가 `HOST_CAN_TX_REQUEST` 중심이라 arm/disarm/heartbeat/session control 계약이 부족하다.
8. neutral/failsafe 정책이 board 수준에서 명확히 고정되어 있지 않다.
9. MCP2515 TX audit pending 구조는 있지만 multi request queue, backpressure, queue full reason이 충분히 구조화되어 있지 않다.
10. HIL 검증 로그와 acceptance 기준이 코드 변경 단위별로 자동화되어 있지 않다.

---

## 3. 최상위 아키텍처 결정

### 3-1. 채택

- CSM은 typed evidence + safety-gated control gateway로 확정한다.
- Live production output은 typed stream 전용으로 고정한다.
- Legacy 20-byte는 보드 live 출력에서 제거하거나 비활성화한다.
- `CONTROL_ACK`와 `CAN_TX_RAW`를 완전히 분리한다.
- `CAPABILITY`와 `BOARD_HEALTH`를 VSM board alive 판단의 필수 조건으로 둔다.
- ADC/전압/health/event를 fake CAN frame으로 만들지 않는다.
- 모든 board-local record는 하나의 monotonic board time axis를 공유한다.
- bus number는 physical channel id로만 취급한다.
- semantic bus role은 VSM model pack/fingerprint/operator override가 결정한다.
- CSM은 physical backend, bitrate, controller type, support flags, health counters를 제공한다.
- control TX는 board safety state, host lease, whitelist, bus readiness가 모두 만족될 때만 허용한다.

### 3-2. 보류

- dual internal CAN0/CAN1 + TJA1051 직접 전환
- CAN FD 지원
- Ethernet/SD card standalone logger
- board-side full CAN DB decode
- board-side vehicle diagnostic semantic 판단
- OTA/remote update
- hard real-time RTOS 구조 전환

### 3-3. 폐기

- CSM을 투명 USB-CAN adapter로 취급하는 구조
- COM open만으로 board alive를 주장하는 구조
- host request 수신만으로 CAN TX 성공 처리
- `CONTROL_ACK accepted`를 최종 성공으로 보는 구조
- bus0=System, bus1=Drive 같은 하드코딩
- health/event/ADC를 임의 CAN ID로 위장하는 구조
- MCP2515 INT_N falling edge 하나에만 의존하는 구조
- HIL 없이 성공으로 문서화하는 방식
- test frame 송신 기능이 production env에서 켜진 상태

---

## 4. 시스템 경계

```text
[VSM Qt workstation]
  -> USB CDC typed downlink
  -> [CSM HostDownlinkParser]
  -> [ControlSession + SafetySupervisor]
  -> [ControlLane]
  -> [CanTxScheduler]
  -> [Physical CAN backends]
      - bus0: MCP2515/TJA1050
      - bus1: Mid Carrier J4 CAN1
  -> actual CAN bus

[Physical CAN backends]
  -> [CanRxLane]
  -> [EvidenceQueue]
  -> [UplinkScheduler]
  -> USB CDC typed uplink
  -> [VSM EvidenceRuntime]
```

CSM은 vehicle semantic decode를 하지 않는다. CSM은 evidence truth와 hardware safety gate를 담당한다. 차량별 값 해석, 에러코드, model pack, bus role resolver는 VSM이 담당한다.

---

## 5. Wire protocol 원칙

### 5-1. Frame header

```text
SOF0              u8  0xA5
SOF1              u8  0x5A
version           u8
record_type       u8
flags             u8
seq               u16_le
payload_len       u16_le
payload           bytes[payload_len]
crc16_ccitt_le    u16_le
```

CRC 범위는 `version`부터 payload 끝까지다. SOF와 CRC trailer는 제외한다.

### 5-2. Required uplink record types

```text
1 CAN_RX_RAW
2 CAN_TX_RAW
5 ADC_SAMPLE
6 CONTROL_ACK
7 BOARD_EVENT
8 BOARD_HEALTH
9 CAPABILITY
```

### 5-3. Required downlink record types

현재 코드의 `HOST_CAN_TX_REQUEST`는 유지한다. 다만 VSM 최종 구조를 위해 다음 downlink command를 추가한다.

```text
10 HOST_CAN_TX_REQUEST        기존 유지
11 HOST_HEARTBEAT             신규
12 HOST_CONTROL_SESSION       신규: arm/disarm/lease/neutral profile install
13 HOST_SET_CONTROL_POLICY    신규: optional, whitelist/profile hash/session policy
14 HOST_QUERY_CAPABILITY      신규 optional
15 HOST_CLEAR_FAULT_LOCKOUT   신규 optional, manual clear only
```

기존 VSM/테스트와의 호환성을 위해 record type 10은 변경하지 않는다. 신규 command를 추가할 때는 `TypedRecords.h`, `shared/docs/TRANSPORT_AND_RECORDS_KO.md`, VSM parser를 같은 변경 단위에서 갱신한다.

### 5-4. Timestamp 원칙

- `CAN_RX_RAW.mono_us`: MCU가 해당 CAN frame을 backend에서 꺼내 evidence queue에 넣는 시점에 최대한 가깝게 기록한다.
- `CAN_TX_RAW.mono_us`: 실제 CAN backend write 성공 또는 TX completion이 확인된 시점에 기록한다.
- `CONTROL_ACK.mono_us`: request validation 결과를 결정한 시점에 기록한다.
- `BOARD_HEALTH.mono_us`: health snapshot 생성 시점.
- VSM은 PC 수신시각을 보조정보로만 사용하고, primary timeline은 board `mono_us`다.

---

## 6. Record payload 설계 보강

### 6-1. CAN_RX_RAW

현재 30-byte payload 구조는 유지한다.

```text
mono_us           u64_le
can_id_flags      u32_le
 dlc_flags        u8
bus               u8
data[8]           u8[8]
rx_total          u32_le
rx_dropped_total  u32_le
```

보강 요구:

- `bus`는 physical bus id다.
- `can_id_flags` lower 29bit는 CAN ID, bit29 EXT, bit30 RTR, bit31 reserved/error로 유지한다.
- dlc는 Classic CAN 기준 0..8만 허용한다.
- VSM은 같은 can_id라도 bus별로 별도 stream으로 취급한다.

### 6-2. CAN_TX_RAW

현재 30-byte payload 구조는 유지한다.

```text
mono_us           u64_le
can_id_flags      u32_le
dlc_flags         u8
bus               u8
data[8]           u8[8]
tx_total          u32_le
tx_failed_total   u32_le
```

보강 요구:

- `CAN_TX_RAW`는 `CONTROL_ACK accepted` 후 실제 backend write 성공이 확인된 경우에만 발행한다.
- MCP2515의 경우 `sendMessage()` 즉시 성공만으로 충분하지 않다. TXB completion 또는 TXREQ clear/TX interrupt 확인 후 성공 audit을 발행한다.
- write fail, timeout, bus-off, all tx busy는 `CAN_TX_RAW`가 아니라 `BOARD_EVENT` + rejected/failed counter로 남긴다.

### 6-3. CONTROL_ACK

현재 payload 구조는 유지하되 reason code를 확장한다.

```text
mono_us                 u64_le
command_id              u32_le
status                  u8    0 reject, 1 accept
reason                  u8
bus                     u8
dlc                     u8
can_id_flags            u32_le
accepted_or_request_cnt u32_le
rejected_total          u32_le
```

권장 reason code:

```text
0x00 OK
0x01 BAD_LENGTH
0x02 BAD_BUS
0x03 UNSUPPORTED_FRAME
0x04 DLC_OUT_OF_RANGE
0x05 ID_NOT_ALLOWED
0x06 CAN_NOT_READY
0x07 CAN_WRITE_FAILED
0x08 BAD_PROTOCOL
0x09 NOT_ARMED
0x0A HOST_TIMEOUT
0x0B CONTROL_LEASE_EXPIRED
0x0C SAFETY_LOCKOUT
0x0D ESTOP_ASSERTED
0x0E FIELD_POWER_LOST
0x0F ENCODER_FAULT
0x10 QUEUE_FULL
0x11 TX_BUSY
0x12 BUS_OFF
0x13 ERROR_PASSIVE
0x14 ROLE_UNRESOLVED
0x15 POLICY_HASH_MISMATCH
0x16 NEUTRAL_PROFILE_MISSING
0x17 RATE_LIMITED
0x18 UNSUPPORTED_COMMAND
```

### 6-4. BOARD_HEALTH v2

현재 52-byte health는 VSM 진단기로는 부족하다. backward compatibility를 위해 기존 health를 유지하면서 v2 확장 payload를 별도 길이로 허용하거나 `BOARD_HEALTH_EXT`를 추가한다.

권장 v2 필드:

```text
mono_us                         u64_le
uptime_ms                       u32_le
main_loop_counter               u32_le
serial_record_tx_total          u32_le
usb_tx_drop_total               u32_le
host_frame_total                u32_le
host_crc_fail_total             u32_le
host_seq_gap_total              u32_le
host_can_tx_request_total       u32_le
host_can_tx_accepted_total      u32_le
host_can_tx_rejected_total      u32_le
can_rx_total_bus0               u32_le
can_rx_drop_bus0                u32_le
can_tx_total_bus0               u32_le
can_tx_fail_bus0                u32_le
can_rx_total_bus1               u32_le
can_rx_drop_bus1                u32_le
can_tx_total_bus1               u32_le
can_tx_fail_bus1                u32_le
mcp_spi_error_total             u32_le
mcp_error_flag_total            u32_le
mcp_rx_overflow_total           u32_le
mcp_last_canintf                u8
mcp_last_eflg                   u8
mcp_last_canctrl                u8
mcp_int_low                     u8
bus0_state                      u8
bus1_state                      u8
safety_state                    u8
control_state                   u8
host_heartbeat_age_ms           u16_le
tx_queue_depth                  u16_le
rx_queue_depth                  u16_le
fault_flags                     u32_le
input_flags                     u16_le
capability_hash                 u32_le
policy_hash                     u32_le
```

### 6-5. CAPABILITY v2/v3

CAPABILITY는 VSM이 board alive와 feature support를 판단하는 핵심이다.

필수 포함:

```text
protocol_version
firmware_version_major/minor/patch
build_id or build_hash
hardware_profile_id
board_id optional
bus_count
per-bus physical descriptor
supported record types
supported downlink commands
queue sizes
safety features
control features
adc features
encoder features
known limitations
```

bus descriptor는 semantic role을 확정하지 말고 physical/backend 중심이어야 한다.

```text
bus_id
physical_connector_kind
backend_kind
transceiver_kind
nominal_bitrate
classic_can_supported
can_fd_supported
rx_supported
tx_supported
control_tx_supported
termination_policy
isolation_policy
level_shift_policy
role_hint_non_authoritative
```

role field가 기존 parser 호환 때문에 필요하면 `role=0 unknown`을 기본으로 하고, VSM에서 fingerprint/model pack으로 System/Drive를 판정하게 한다. production env에서 `BOARD_MCP2515_BUS_ROLE=1`, `BOARD_BUILTIN_CAN_BUS_ROLE=2`처럼 semantic을 박아두는 것은 새 기준에서는 폐기하거나 `role_hint`로 격하시킨다.

---

## 7. Safety / control state machine

### 7-1. 상태 정의

기존 `SafetyState`는 아래 상태 체계로 재정의한다.

```text
BOOT
MONITOR_ONLY
READY
ARMED
CONTROL_ACTIVE
HOST_TIMEOUT
FAULT_LOCKOUT
ESTOP
```

### 7-2. 상태 의미

| State | 의미 | TX 허용 |
|---|---|---|
| BOOT | 초기화 중 | 금지 |
| MONITOR_ONLY | 기본 상태, RX/health만 허용 | 금지 |
| READY | hardware OK, host alive, 아직 arm 안 됨 | 금지 |
| ARMED | manual arm + host lease 유효 | whitelist TX만 허용 |
| CONTROL_ACTIVE | 최근 control request가 정상 처리 중 | whitelist TX만 허용 |
| HOST_TIMEOUT | heartbeat/lease 만료 | 신규 TX 금지, neutral 정책 가능 |
| FAULT_LOCKOUT | field/encoder/CAN fault | 금지 |
| ESTOP | estop asserted | 금지 |

### 7-3. 기본값

- 부팅 후 기본 상태는 `MONITOR_ONLY`다.
- USB 재연결 후 자동 arm 금지.
- VSM reconnect 후에도 manual arm 또는 explicit session command가 필요하다.
- `CanTxEnable` 출력은 기본 LOW다.
- test TX env가 아닌 production env에서 주기 test frame 송신 금지.

### 7-4. Host heartbeat / lease

VSM은 `HOST_HEARTBEAT`를 50~100 ms 주기로 송신한다. CSM은 마지막 heartbeat age를 health에 올린다.

권장 timeout:

```text
heartbeat_warn_ms = 150
heartbeat_timeout_ms = 300
control_lease_ms = 500
```

동작:

- heartbeat age > warn: `BOARD_EVENT HostHeartbeatWarn`
- heartbeat age > timeout: `HOST_TIMEOUT`, 신규 TX reject reason `HOST_TIMEOUT`
- lease expired: `CONTROL_LEASE_EXPIRED`, 신규 TX reject
- heartbeat 재개만으로 자동 ARMED 복귀 금지. VSM이 다시 arm/session을 보내야 한다.

### 7-5. Arm/disarm

`HOST_CONTROL_SESSION` command로 arm/disarm을 명시한다.

필드 제안:

```text
command_id       u32_le
action           u8  0 disarm, 1 arm, 2 renew_lease, 3 install_neutral_profile
requested_bus    u8  0..n or 0xFF any
flags            u16_le
lease_ms         u16_le
policy_hash      u32_le
model_pack_hash  u32_le
reserved/data    variable
```

CSM은 다음 조건을 모두 만족해야 arm을 허용한다.

- physical safety input OK
- estop not asserted
- field power OK
- encoder fault not asserted
- CAN backend ready
- host heartbeat alive
- allowed ID whitelist valid
- control policy hash valid 또는 default policy explicit
- no fault lockout

### 7-6. Neutral/failsafe

중요: CSM이 차량 neutral 의미를 임의 추정하면 안 된다.

기본 정책:

- neutral profile이 설치/확정되지 않은 경우 timeout 시 신규 TX 차단만 수행한다.
- neutral profile이 설치되어 있고 arm 상태였던 경우에만 timeout 시 neutral burst를 허용한다.
- neutral burst도 반드시 `CAN_TX_RAW` audit 대상이다.
- neutral burst 실패는 `BOARD_EVENT`로 올린다.

HAMT2 임시 profile 후보:

```text
0x510 = 40 00 00 00 00 00 00 00
0x511 = 40 00 00 00 00 00 00 00
0x512 = 40 00 00 00 00 00 00 00
0x513 = 40 00 00 00 00 00 00 00
```

단, 이 profile은 user-confirmed 또는 VSM-installed policy로만 활성화한다. CSM default production firmware가 임의로 차량 제어 neutral frame을 자동 송신하면 안 된다.

---

## 8. CAN backend 설계

### 8-1. 공통 interface

`src/main.cpp`에서 backend 구현을 분리한다.

```text
include/board/can/CanTypes.h
include/board/can/ICanBackend.h
src/board/can/Mcp2515Backend.cpp
src/board/can/ArduinoCanBackend.cpp
src/board/can/CanRxLane.cpp
src/board/can/CanTxScheduler.cpp
src/board/can/CanTxAuditLane.cpp
```

공통 backend interface:

```cpp
struct CanFrame8 {
  uint64_t mono_us;
  uint32_t can_id_flags;
  uint8_t dlc;
  uint8_t data[8];
};

enum class CanBackendState : uint8_t {
  NotPresent,
  InitFailed,
  Ready,
  ErrorWarning,
  ErrorPassive,
  BusOff,
  SpiFault,
};

class ICanBackend {
 public:
  virtual bool begin() = 0;
  virtual bool pollRx(CanFrame8& out) = 0;
  virtual TxSubmitResult submitTx(const CanFrame8& frame, uint32_t command_id) = 0;
  virtual void serviceTxAudit() = 0;
  virtual CanBackendState state() const = 0;
  virtual CanBackendHealth health() const = 0;
};
```

### 8-2. MCP2515 backend

MCP2515 고정 조건:

```text
CAN_500KBPS
MCP_8MHZ
SPI 2 MHz default
INT_N active-low level hint
bounded polling fallback
one-shot mode optional for audit
```

금지:

- INT falling edge 단독 의존
- 16 MHz clock 설정
- sendMessage 즉시 성공을 최종 TX audit으로 처리
- RX overflow를 health/event 없이 조용히 clear

필수:

- CANINTF/EFLG/CANCTRL snapshot
- RX0OVR/RX1OVR counter
- TXBO/TXEP/MERRF event
- TX timeout 후 ABAT 처리
- TXB busy 시 reason `TX_BUSY` 또는 `QUEUE_FULL`

### 8-3. J4 Arduino_CAN backend

J4 CAN1 조건:

- `CAN.begin(CanBitRate::BR_500k)` 성공 필요
- SW2 CAN enabled 상태 필요
- J4 GND/reference 연결 권장
- RX/TX 성공은 health에 별도 counter로 분리

금지:

- built-in lane begin 성공만으로 실제 physical TX 검증 완료 주장
- PCAN/Kvaser 수신 확인 없는 HIL success 문서화

---

## 9. Control lane 설계

### 9-1. command 처리 순서

```text
HostDownlinkParser
  -> CRC/length/protocol 검증
  -> command decoder
  -> SafetySupervisor gate
  -> ControlPolicy gate
  -> CanTxScheduler enqueue/submit
  -> CONTROL_ACK accepted/rejected
  -> backend tx completion/fail
  -> CAN_TX_RAW or BOARD_EVENT
```

### 9-2. CONTROL_ACK timing

두 가지 방식을 명확히 구분한다.

권장 방식:

- request가 검증되고 TX queue에 들어가면 `CONTROL_ACK accepted` 발행
- 실제 backend TX completion 후 `CAN_TX_RAW` 발행

거절 방식:

- 검증 실패, safety fail, queue full, backend not ready면 `CONTROL_ACK rejected` 발행
- 이 경우 `CAN_TX_RAW` 발행 금지

### 9-3. TX queue

현재 직접 submit 구조를 queue 기반으로 바꾼다.

```text
ControlTxQueue size 32 권장
item:
  command_id
  bus
  can_id_flags
  dlc
  data[8]
  enqueue_mono_us
  deadline_ms
  source = host / neutral / test
```

CSM은 queue depth를 health에 올린다. queue full은 `CONTROL_ACK rejected reason=QUEUE_FULL`이다.

### 9-4. Allowlist

현재 allowlist 유지:

```text
0x503
0x510..0x513
standard frame only
RTR forbidden
DLC 0..8 only
```

개선:

- allowlist를 `ControlPolicy` 모듈로 분리
- policy hash를 CAPABILITY/HEALTH에 표시
- VSM model pack hash와 mismatch면 TX reject 가능
- extended frame은 기본 거절

---

## 10. Uplink scheduler / backpressure

현재 CSM은 CAN RX drain, health, capability, encoder, ADC, event를 loop에서 직접 emit한다. 이를 `UplinkScheduler`로 분리한다.

우선순위:

1. `BOARD_EVENT` critical
2. `CONTROL_ACK`
3. `CAN_TX_RAW`
4. `CAN_RX_RAW`
5. `BOARD_HEALTH`
6. `CAPABILITY`
7. `ADC_SAMPLE`
8. encoder derived/debug

요구:

- Serial write 실패/blocked 가능성을 고려한 drop counter
- CAN RX queue overflow event
- record type별 emitted/dropped counter
- health/capability는 주기 송신하되 CAN burst를 과도하게 막지 않게 rate limit
- `Serial.flush()`를 loop에서 남발 금지

---

## 11. 폴더 구조 재정리안

현재 단일 `src/main.cpp`를 아래 구조로 단계적으로 분리한다.

```text
include/
  BoardPins.h
  protocol/
    TypedFrame.h
    TypedRecords.h
  board/
    BuildConfig.h
    BoardTime.h
    BoardEvents.h
    CapabilityPublisher.h
    HealthMonitor.h
    HostDownlinkParser.h
    UplinkScheduler.h
    SafetySupervisor.h
    ControlPolicy.h
    ControlLane.h
    can/
      CanTypes.h
      ICanBackend.h
      Mcp2515Backend.h
      ArduinoCanBackend.h
      CanRxLane.h
      CanTxScheduler.h
    adc/
      AnalogSampleLane.h
    encoder/
      EncoderLane.h

src/
  main.cpp
  protocol/
    TypedFrame.cpp
  board/
    BoardTime.cpp
    CapabilityPublisher.cpp
    HealthMonitor.cpp
    HostDownlinkParser.cpp
    UplinkScheduler.cpp
    SafetySupervisor.cpp
    ControlPolicy.cpp
    ControlLane.cpp
    can/
      Mcp2515Backend.cpp
      ArduinoCanBackend.cpp
      CanRxLane.cpp
      CanTxScheduler.cpp
    adc/
      AnalogSampleLane.cpp
    encoder/
      EncoderLane.cpp

test/ 또는 native_tests/
  test_typed_frame.cpp
  test_host_downlink_parser.cpp
  test_control_policy.cpp
  test_safety_supervisor.cpp
  test_capability_payload.cpp
  test_health_payload.cpp
  test_tx_audit_state.cpp

pc_tools/
  verify_typed_stream.py
  send_host_can_tx_request.py
  exercise_host_session.py
  parse_capture_summary.py
```

`main.cpp`는 setup/loop orchestration만 남긴다.

---

## 12. PlatformIO env 정리

### 12-1. production env

유지:

```text
portenta_h7_m7_mid_mcp2515_j4_dual_csm
```

조건:

- `BOARD_ENABLE_MCP2515_TX_TEST=0`
- `BOARD_ENABLE_BUILTIN_CAN_TX_TEST=0`
- `BOARD_ENABLE_HOST_CAN_TX_MCP2515=1`
- `BOARD_ENABLE_HOST_CAN_TX_BUILTIN=1`
- `BOARD_MCP2515_SPI_HZ=2000000`
- `MCP_8MHZ`, `CAN_500KBPS` 코드 고정
- semantic role build flag 제거 또는 non-authoritative 처리

### 12-2. smoke/test env

유지 가능:

```text
portenta_h7_m7_mid_mcp2515_j4_dual_smoke
portenta_h7_m7_mid_mcp2515_csm_tx_test
```

조건:

- 이름에 반드시 `smoke` 또는 `tx_test` 명시
- production env와 분리
- test frame enabled 여부를 CAPABILITY limitation flag에 표시

### 12-3. deferred env

보류:

```text
portenta_h7_m7_mid_dual_can20
BOARD_HW_PROFILE_MID_TJA1051_DUAL
```

이 env는 현재 활성 production 기준이 아니다. HIL 완료 전 VSM/CSM mainline 기준으로 쓰지 않는다.

---

## 13. HIL / 검증 기준

### 13-1. Build gate

필수:

```powershell
platformio run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm
```

성공 기준:

- compile PASS
- warning 중 protocol size/overflow 관련 치명 warning 없음

### 13-2. Boot typed stream gate

검증:

```powershell
python pc_tools/verify_typed_stream.py --port COMx --duration 10
```

성공 기준:

- `CAPABILITY` 수신
- `BOARD_HEALTH` 수신
- CRC fail 0
- seq gap 0 또는 원인 기록
- unknown record 없음

### 13-3. CAN RX gate

bus0 MCP2515:

- PCAN/Kvaser로 500 kbps frame 송신
- CSM `CAN_RX_RAW bus=0` 확인
- rx_total 증가
- drop/overflow 0

bus1 J4:

- Kvaser/PCAN으로 J4 CANH/CANL 연결
- SW2 CAN enabled
- CSM `CAN_RX_RAW bus=1` 확인
- rx_total 증가
- error frame 없음

### 13-4. Host TX audit gate

VSM 또는 pc_tools에서 host request 송신:

```text
bus=0 id=0x503 payload=...
bus=1 id=0x503 payload=...
```

성공 기준:

- `CONTROL_ACK accepted` 수신
- 이후 matching `CAN_TX_RAW` 수신
- 외부 PCAN/Kvaser에서 동일 ID/DLC/data 확인
- `CONTROL_ACK`만 있고 `CAN_TX_RAW`가 없으면 성공 아님

### 13-5. Reject gate

반드시 실패 케이스를 검증한다.

- unsupported bus
- extended frame
- RTR frame
- DLC > 8
- ID not allowed
- not armed
- host timeout
- queue full
- backend not ready

각 케이스는 `CONTROL_ACK rejected`와 정확한 reason을 반환해야 한다.

### 13-6. Safety gate

검증:

- boot 후 기본 `MONITOR_ONLY`
- arm 전 TX reject reason `NOT_ARMED`
- heartbeat timeout 후 TX reject reason `HOST_TIMEOUT`
- estop asserted 후 `ESTOP`, TX reject
- field power lost 후 `FAULT_LOCKOUT`, TX reject
- reconnect 후 자동 arm 금지

### 13-7. Neutral gate

검증:

- neutral profile 미설치 시 timeout에서 neutral 송신 금지
- neutral profile 설치 시 timeout에서 neutral burst 송신
- neutral burst 각 frame은 `CAN_TX_RAW` audit 필수
- neutral 송신 실패는 `BOARD_EVENT` 필수

---

## 14. Codex 작업 지시문

아래 내용을 Codex에 그대로 전달한다.

```text
작업 목표:
HAMT2-platform CSM을 Portenta H7 M7 + Mid Carrier ASX00055 + MCP2515/TJA1050 bus0 + J4 CAN1 bus1 기준의 typed evidence + safety-gated control gateway로 재정리한다. 기존 live legacy 20-byte 출력 확장 방향은 버리고, production truth는 typed stream으로 고정한다.

최상위 계약:
1. CSM은 USB-CAN 투명 브릿지가 아니다.
2. CONTROL_ACK는 host request 수락/거절 evidence일 뿐이며 실제 CAN 송신 성공이 아니다.
3. 실제 CAN 송신 성공은 backend write/tx completion 후 발행하는 CAN_TX_RAW로만 판단한다.
4. CAPABILITY + BOARD_HEALTH 수신 전 VSM은 board alive로 보면 안 된다.
5. bus0/bus1은 physical channel id일 뿐 System/Drive role을 하드코딩하지 않는다.
6. ADC/health/event/safety를 fake CAN frame으로 만들지 않는다.
7. HIL 없이 physical success를 문서화하지 않는다.

수정 대상:
- src/main.cpp를 orchestration 중심으로 축소한다.
- include/protocol/TypedRecords.h에 필요한 신규 downlink command/reason/status를 추가한다.
- shared/docs/TRANSPORT_AND_RECORDS_KO.md와 docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO.md를 함께 갱신한다.
- SafetySupervisor, ControlPolicy, ControlLane, CanTxScheduler, HealthMonitor, UplinkScheduler, Mcp2515Backend, ArduinoCanBackend 모듈을 분리한다.
- pc_tools에 heartbeat/session/arm/reject/audit 검증 스크립트를 추가한다.

필수 구현:
1. HOST_HEARTBEAT downlink 추가.
2. HOST_CONTROL_SESSION downlink 추가. arm/disarm/lease renew를 지원한다.
3. boot default는 MONITOR_ONLY다.
4. arm 전 host TX는 CONTROL_ACK rejected reason NOT_ARMED다.
5. heartbeat/lease timeout 시 신규 TX는 HOST_TIMEOUT 또는 CONTROL_LEASE_EXPIRED로 reject한다.
6. CONTROL_ACK accepted 후 실제 CAN_TX_RAW audit을 분리 유지한다.
7. MCP2515는 MCP_8MHZ + CAN_500KBPS 고정이다.
8. MCP2515 INT_N은 level hint + bounded polling fallback으로 유지한다. falling edge 단독 의존 금지.
9. BOARD_HEALTH에 host crc fail, request/accept/reject, bus별 rx/tx/drop/fail, MCP error flags, queue depth, safety state, heartbeat age를 넣는다.
10. CAPABILITY에 bus별 physical backend/controller/transceiver/bitrate/support flags를 넣고 semantic role은 authoritative로 쓰지 않는다.
11. production env에서는 TX test frame을 꺼둔다.
12. neutral profile 미설치 상태에서 timeout neutral 자동 송신 금지. profile 설치 시에만 neutral burst 가능.

검증:
- platformio run -e portenta_h7_m7_mid_mcp2515_j4_dual_csm
- verify_typed_stream.py로 CAPABILITY/BOARD_HEALTH/CRC/seq 확인
- bus0 RX/TX audit HIL
- bus1 RX/TX audit HIL
- reject reason matrix 검증
- safety state matrix 검증
- timeout/neutral profile 검증

결과물:
- 수정된 코드
- 갱신된 docs/architecture 문서
- 갱신된 board/BRIEF.md
- HIL runbook
- pc_tools 검증 스크립트
- build/test 결과 요약
- 변경된 wire contract 요약

금지:
- CONTROL_ACK만으로 송신 성공 처리하지 말 것.
- bus role을 build flag로 확정하지 말 것.
- ADC/health/event를 fake CAN으로 만들지 말 것.
- HIL 없이 성공이라고 쓰지 말 것.
- production env에서 periodic test TX를 켜지 말 것.
- MCP2515 16MHz로 설정하지 말 것.
```

---

## 15. 구현 우선순위

### Phase 1. 계약 동결

- `TypedRecords.h` reason/command 확장
- shared transport docs 갱신
- payload length/version 정책 확정
- CAPABILITY/HEALTH v2 정책 확정

### Phase 2. 코드 분리

- `main.cpp` orchestration 축소
- `SafetySupervisor`
- `ControlPolicy`
- `ControlLane`
- `CanTxScheduler`
- `HealthMonitor`
- `UplinkScheduler`

### Phase 3. safety/control 구현

- host heartbeat
- control session arm/disarm/lease
- not armed reject
- timeout reject
- queue full reject
- neutral profile optional

### Phase 4. backend health 강화

- MCP2515 health counters
- J4 CAN health counters
- bus별 rx/tx/fail/drop 분리
- BOARD_EVENT code 확장

### Phase 5. HIL 검증

- boot stream
- bus0 RX
- bus0 TX audit
- bus1 RX
- bus1 TX audit
- reject matrix
- safety matrix
- timeout/neutral matrix

---

## 16. acceptance checklist

- [ ] `portenta_h7_m7_mid_mcp2515_j4_dual_csm` build PASS
- [ ] boot 후 `CAPABILITY` 1회 이상 수신
- [ ] `BOARD_HEALTH` 주기 수신
- [ ] CRC fail 0
- [ ] host heartbeat age health 표시
- [ ] boot default `MONITOR_ONLY`
- [ ] arm 전 TX reject `NOT_ARMED`
- [ ] arm 후 allowlisted TX accepted
- [ ] accepted TX에 대해 실제 `CAN_TX_RAW` audit 발생
- [ ] 외부 PCAN/Kvaser에서 동일 frame 확인
- [ ] unsupported ID reject `ID_NOT_ALLOWED`
- [ ] unsupported bus reject `BAD_BUS`
- [ ] heartbeat timeout 후 TX reject `HOST_TIMEOUT`
- [ ] estop/field fault에서 TX reject
- [ ] MCP2515 8MHz/500kbps 확인
- [ ] production env test TX disabled
- [ ] bus role hardcoding 제거 또는 non-authoritative 처리
- [ ] docs/architecture 갱신
- [ ] board/BRIEF.md 갱신

---

## 17. 최종 결론

CSM은 현재 하드웨어를 유지하되 펌웨어 구조는 더 바뀌어야 한다. 바뀌어야 하는 핵심은 CAN 드라이버 자체보다 **evidence 계약, safety gate, health/capability, control audit chain**이다.

가장 중요한 판단은 하나다.

```text
VSM이 보낸 명령은 성공이 아니다.
CSM이 실제 CAN backend 송신 성공을 확인하고 올린 CAN_TX_RAW만 성공이다.
```

이 기준을 지키면 VSM은 CAN 제어, 진단, 로그, 리플레이, 모델팩 에러 해석을 같은 evidence chain 위에서 안정적으로 구현할 수 있다.
