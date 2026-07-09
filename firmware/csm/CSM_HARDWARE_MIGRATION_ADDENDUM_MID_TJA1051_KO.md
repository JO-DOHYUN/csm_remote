# CSM 하드웨어 변경 추가 구상안 — Portenta H7 + Mid Carrier + TJA1051T/3

## 0. 목적

이번 문서는 기존 CSM 구상안에 대한 **하드웨어 변경 추가 결정서**다.

기존 기준:

- Arduino Portenta H7 M7 + Portenta HAT Carrier
- MCP2515 + TJA1050 외부 CAN controller/transceiver 조합
- 2채널 CAN: MCP2515 bus + Portenta built-in CAN bus 혼합

변경 기준:

- Arduino Portenta H7 M7 + Portenta Mid Carrier `ASX00055`
- Adafruit `ada-5708`, TJA1051T/3 계열 CAN transceiver 사용
- MCP2515 외부 CAN controller 삭제
- Portenta H7 내부 FDCAN/CAN controller 2개 lane을 직접 사용
- 외부 부품은 CAN controller가 아니라 **물리 계층 transceiver** 역할만 담당

핵심 판단:

> 이번 변경은 단순 핀 변경이 아니다.  
> CSM의 bus 의미, capability 계약, health/error 지표, PlatformIO env, Qt/VMS의 bus 표시 방식까지 바뀌는 아키텍처 변경이다.

---

## 1. 현재 csm(2) 기준 확인 결과

`J_ArdP7_AM2_CSM (2).zip` 기준 현재 구조는 이미 다음 방향으로 정리돼 있다.

- root `AGENTS.md`가 `BRIEF.md`, `board/AGENTS.md`, `qt/AGENTS.md`, `shared/docs/TRANSPORT_AND_RECORDS_KO.md`, `VMS_CSM_03_ARCHITECT_SYNTHESIS_FINAL.md`를 읽도록 라우팅한다.
- `BRIEF.md`는 CSM을 단순 USB-CAN adapter가 아니라 typed evidence/control board로 정의한다.
- `board/BRIEF.md`는 현재 CAN baseline을 `MCP2515 bus=0`, `Portenta built-in CAN bus=1`로 정의한다.
- `platformio.ini`는 MCP2515 기반 env와 built-in CAN env가 섞여 있다.
- `include/BoardPins.h`는 MCP2515 SPI 핀 `D7..D11`과 built-in CAN `PH13/PB8`를 함께 설명한다.
- `src/main.cpp`는 아직 단일 파일 중심이지만 typed frame, capability, board health, MCP2515 service, built-in CAN RX/TX, host CAN TX request가 모두 들어가 있다.
- `shared/docs/TRANSPORT_AND_RECORDS_KO.md`는 `CONTROL_ACK`와 `CAN_TX_RAW`를 분리하고, 실제 송신 성공은 `CAN_TX_RAW` audit으로만 본다는 계약을 이미 담고 있다.

따라서 이번 변경은 “새 프로젝트를 다시 만드는 것”이 아니라, 기존 typed evidence/control gateway 구조를 유지하면서 **MCP2515 lane을 dual internal CAN lane 구조로 교체**하는 작업이다.

---

## 2. 하드웨어 의미 재정의

### 2.1 MCP2515 + TJA1050 방식

기존 MCP2515 보드는 두 기능을 동시에 갖는다.

1. CAN controller: SPI로 MCU와 통신하고 CAN frame buffering/filtering/bit timing을 담당
2. CAN transceiver: TJA1050이 CANH/CANL 물리 계층을 담당

그래서 기존 firmware에는 다음이 필요했다.

- SPI init
- MCP2515 bitrate 설정
- MCP RX FIFO drain
- MCP error flag 읽기
- MCP INT_N 처리
- MCP library dependency
- MCP bus를 별도 capability로 표시

### 2.2 TJA1051T/3 transceiver 방식

새 방식의 TJA1051T/3 breakout은 CAN controller가 아니다.

역할은 아래뿐이다.

```text
Portenta H7 내부 CAN_TX/CAN_RX  <->  TJA1051 TXD/RXD  <->  CANH/CANL
```

즉 firmware 관점에서 SPI/MCP2515 layer는 사라지고, Portenta H7 내부 CAN controller/FDCAN peripheral이 직접 frame 송수신을 담당해야 한다.

중요한 표현 정정:

- “외부 드라이버 삭제”보다는 **외부 CAN controller 삭제**가 정확하다.
- TJA1051은 여전히 외부 CAN transceiver/physical driver다.
- Portenta 내부에는 CAN controller가 있고, 외부 TJA1051은 물리 계층 변환기다.

---

## 3. 새 bus mapping 제안

최종 bus 의미는 아래처럼 고정하는 것이 좋다.

| logical bus | physical backend | 역할 | control TX |
|---|---|---|---|
| `bus=0` | Portenta internal CAN lane A + TJA1051 transceiver | System/monitor CAN | 기본 금지 |
| `bus=1` | Portenta internal CAN lane B + TJA1051 transceiver | Drive/control CAN | allowlist 조건부 허용 |

기존 `bus=0 MCP2515`, `bus=1 built-in CAN` 의미는 폐기해야 한다.

단, record type 자체는 바꾸면 안 된다.

- `CAN_RX_RAW` 유지
- `CAN_TX_RAW` 유지
- `CONTROL_ACK` 유지
- `BOARD_HEALTH` 유지
- `CAPABILITY` 유지

바뀌는 것은 record type이 아니라 **CAPABILITY에서 bus별 physical/backend 의미를 설명하는 방식**이다.

---

## 4. 가장 중요한 소프트웨어 리스크

### 4.1 Arduino_CAN이 dual internal CAN을 동시에 지원하는지 검증 필요

현재 `src/main.cpp`는 `Arduino_CAN.h`의 전역 `CAN` 객체를 사용한다.

이 구조는 “하나의 built-in CAN lane”에는 충분하지만, 두 개의 내부 CAN controller를 동시에 쓰려면 부족할 가능성이 있다.

따라서 Codex/펌웨어 작업은 절대 아래를 가정하면 안 된다.

```cpp
CAN0.begin(...);
CAN1.begin(...);
```

이런 객체가 실제 라이브러리에 있는지 먼저 확인해야 한다.

권장 판단:

1. 먼저 현재 ArduinoCore-mbed / Arduino_CAN 구현에서 Portenta H7의 dual CAN instance 지원 여부를 확인한다.
2. 한 개만 지원한다면, dual lane production firmware는 STM32 HAL FDCAN backend로 분리한다.
3. Arduino_CAN 기존 코드는 `BuiltinCanSingleLane` 또는 `LegacyArduinoCanLane`으로 보존한다.
4. dual lane은 `FdcanHalLane` 또는 `PortentaFdcanLane`으로 새로 만든다.

### 4.2 FDCAN peripheral과 CAN FD 기능을 혼동하지 말 것

STM32H747 계열은 FDCAN peripheral을 사용할 수 있지만, 차량 bus가 Classic CAN 500 kbps이면 firmware는 Classic CAN frame, DLC 0..8, FD/BRS off 기준으로 동작해야 한다.

이번 프로젝트의 typed record에서도 현재 payload는 data[8] 기준이다.

따라서 이번 단계에서는:

- CAN FD frame 송수신은 보류
- Classic CAN 11-bit/29-bit, DLC 0..8 기준 유지
- `CAN_FD`, `BRS`, DLC > 8은 protocol future item으로만 둠

---

## 5. BoardPins 변경 방향

현재 `include/BoardPins.h`는 MCP2515 SPI 핀을 production bench 기준으로 포함한다.

새 기준에서는 production pin map을 분리해야 한다.

### 5.1 유지할 핀

- Safety pins: `D0..D3`, `D12..D14` 계열은 일단 유지 가능
- Encoder pins: `D5/PC6`, `D4/PC7`, `D6/PA8` 유지 가능
- Voltage ADC: lab-only fallback이면 유지 가능

### 5.2 production에서 내릴 핀

아래는 MCP2515 전용이므로 production default에서 제거하거나 legacy/diag로 내려야 한다.

| 기존 신호 | Portenta pin | 기존 의미 | 새 기준 |
|---|---|---|---|
| `CanSpiCsN` | D7 / PI0 | MCP2515 CS | production default 사용 안 함 |
| `SpiCopi` | D8 / PC3 | MCP2515 SI | production default 사용 안 함 |
| `SpiSck` | D9 / PI1 | MCP2515 SCK | production default 사용 안 함 |
| `SpiCipo` | D10 / PC2 | MCP2515 SO | production default 사용 안 함 |
| `CanIntN` | D11 / PH8 | MCP2515 INT | production default 사용 안 함 |

### 5.3 새 CAN pin 정의

`BoardPins.h`에는 Arduino pin alias만 넣기보다 STM32 port 기준 이름을 명확히 적어야 한다.

예시:

```cpp
namespace BoardPins {
namespace CanPhysical {
  // TODO: confirm with Portenta H7 + Mid Carrier pinout/schematic.
  // CAN lane A: STM32 FDCANx_TX/RX -> TJA1051 #0 TXD/RXD
  // CAN lane B: STM32 FDCANy_TX/RX -> TJA1051 #1 TXD/RXD
}
}
```

주의:

- 기존 문서에는 `PH13/PB8`가 built-in CAN lane으로 기록돼 있다.
- 두 번째 내부 CAN lane의 실제 노출 핀은 Portenta H7 pinout + Mid Carrier schematic으로 확정해야 한다.
- 실물 carrier 실크와 데이터시트 표가 다르게 보이는 경우, 최종 기준은 반드시 Arduino 공식 pinout/schematic + STM32 alternate function + 실제 continuity 확인 순서로 확정한다.

---

## 6. PlatformIO env 변경 방향

현재 env는 MCP 중심 이름이 많다.

새 구조에서는 production env를 새로 만들고, MCP env는 legacy/diag로 남기는 것이 안전하다.

### 6.1 새 production env

```ini
[env:portenta_h7_m7_mid_dual_fdcan]
extends = env:portenta_h7_m7
build_flags =
  -D BOARD_HW_PROFILE_MID_TJA1051_DUAL=1
  -D BOARD_ENABLE_MCP2515=0
  -D BOARD_ENABLE_FDCAN_LANE0=1
  -D BOARD_ENABLE_FDCAN_LANE1=1
  -D BOARD_ENABLE_HOST_CAN_TX=1
  -D BOARD_CONTROL_BUS=1
  -D BOARD_ENABLE_VOLTAGE_ADC=1
  -D BOARD_VOLTAGE_SAMPLE_PERIOD_MS=20
```

### 6.2 MCP env 처리

MCP env는 바로 삭제하지 말고 아래처럼 처리한다.

- `legacy_mcp_*` 또는 `bench_mcp_*` 이름으로 이동
- root/board BRIEF에는 “old bench profile”로 명시
- production default에서는 빌드 대상에서 제외
- MCP 관련 docs는 history/legacy로 내림

---

## 7. Firmware architecture 변경 방향

### 7.1 CanLane abstraction 필요

현재 `main.cpp`는 MCP2515와 Arduino_CAN이 직접 섞여 있다.

새 구조에서는 아래 추상화가 필요하다.

```text
CanLane
  - begin(bitRate, mode)
  - pollRx(budget)
  - write(frame)
  - serviceHealth()
  - getCounters()
  - getCapabilityDescriptor()
```

구현체:

```text
LegacyMcp2515Lane       // 기존 bench/legacy only
ArduinoCanSingleLane    // 기존 Arduino_CAN 전역 CAN 객체 기반
PortentaFdcanHalLane0   // 새 production candidate
PortentaFdcanHalLane1   // 새 production candidate
```

### 7.2 main.cpp 역할 축소

`src/main.cpp`는 다음만 담당해야 한다.

- setup orchestration
- loop scheduler
- lane service 호출
- typed record uplink 호출
- safety/control supervisor 호출

직접 SPI, MCP, FDCAN register, host command parse, health payload 조립을 계속 넣으면 다음 변경 때 다시 깨진다.

### 7.3 Host control path

기존 allowlist는 유지하되 bus 의미를 바꾼다.

기존:

```text
control bus = 1 = Portenta built-in CAN
```

새 기준:

```text
control bus = 1 = Drive/control FDCAN lane through TJA1051 #1
```

allowlist:

- `0x503`
- `0x510`
- `0x511`
- `0x512`
- `0x513`

규칙:

- host command 수신
- syntax/safety/allowlist 검증
- `CONTROL_ACK` accept/reject
- 실제 CAN write
- 성공 시 `CAN_TX_RAW`
- 실패 시 `BOARD_EVENT` + health counter

`CONTROL_ACK`만 성공으로 보면 안 된다.

---

## 8. CAPABILITY v2 필요성

현재 36-byte `CAPABILITY` payload는 기존 의미가 너무 MCP/built-in 혼합에 묶여 있다.

현재 bit 의미 예:

- bit0: MCP bus0 RX compiled
- bit1: built-in bus1 RX compiled
- bit2: built-in bus1 TX compiled
- bit3: host TX policy active on bus1
- bit4: MCP INT_N level hint compiled

새 hardware에서는 이 의미가 맞지 않는다.

따라서 두 방법 중 하나를 선택해야 한다.

### 선택 A: 36-byte CAPABILITY 유지 + profile_major bump

장점:

- Qt 수정량 작음
- 기존 parser가 크게 안 깨짐

단점:

- bus별 backend/role 설명이 부족함
- bit 의미 재사용 때문에 혼동 가능

### 선택 B: CAPABILITY payload 확장

권장안은 B다.

`record_type=9 CAPABILITY`는 유지하되, `payload_len`을 늘리고 `board_profile_major=2`로 올린다.

추가해야 할 필드:

```text
bus_count
for each bus:
  bus_id
  physical_backend_type   // FDCAN_HAL, ARDUINO_CAN, MCP2515_LEGACY
  transceiver_type        // TJA1051T3, TJA1050_LEGACY, HAT_CARRIER, UNKNOWN
  role                    // SYSTEM, DRIVE_CONTROL, DEBUG
  rx_supported
  tx_supported
  control_tx_allowed
  classic_can_supported
  can_fd_supported
  max_dlc
  nominal_bitrate
  data_bitrate
  termination_policy
  isolation_policy
```

Qt/VMS는 profile_major=2를 받으면 bus label을 하드코딩하지 않고 capability에서 표시해야 한다.

---

## 9. BOARD_HEALTH v2 방향

MCP 관련 지표는 production health에서 빠져야 한다.

기존 health:

- MCP INT flag
- MCP error flags
- built-in CAN ready flag

새 health:

```text
per bus:
  rx_total
  tx_request_total
  tx_success_total
  tx_fail_total
  rx_dropped_total
  bus_off_or_unavailable
  error_passive_or_unavailable
  last_error_code
  api_limit_flags

board-level:
  usb_queue_depth
  host_rx_total
  host_crc_fail_total
  safety_state
  control_lease_state
  field_power_ok
  transceiver_standby_state
```

Arduino_CAN/HAL이 bus-off/error-passive를 노출하지 못하면 값을 숨기지 말고 `api_limit_flags`에 “not exposed”로 표시한다.

---

## 10. Qt/VMS 변경 방향

Qt는 record type 자체를 크게 바꿀 필요는 없다.

하지만 다음 가정은 반드시 제거해야 한다.

```text
bus=0 == MCP2515
bus=1 == built-in CAN
```

새 Qt/VMS 기준:

- bus label은 CAPABILITY에서 온다.
- `bus=0`: System/monitor CAN
- `bus=1`: Drive/control CAN
- physical backend: both internal FDCAN + TJA1051 transceiver
- MCP-related UI label은 legacy profile에서만 표시
- board alive 판단은 COM open이 아니라 CAPABILITY 수신 기준
- control success는 CAN_TX_RAW audit 기준
- CONTROL_ACK는 request decision timeline에 표시
- health panel은 per-bus counters를 표시

Qt parser는 다음을 지원해야 한다.

- CAPABILITY v1: 기존 MCP/built-in profile compatibility
- CAPABILITY v2: new dual-FDCAN/Mid/TJA1051 profile
- unknown longer CAPABILITY payload: 최소 공통부 파싱 + 나머지 safe skip

---

## 11. 배선/전기적 검증 체크리스트

실제 배선은 반드시 Adafruit 5708 schematic 기준으로 확정한다.

일반 원칙:

- Portenta CAN_TX -> TJA1051 TXD
- Portenta CAN_RX <- TJA1051 RXD
- Portenta GND -> transceiver GND
- TJA1051 CANH/CANL -> 차량/PCAN CANH/CANL
- transceiver logic supply는 Portenta 3.3 V logic과 호환되어야 함
- standby/silent pin이 있으면 기본 normal mode가 되도록 pull 또는 GPIO 제어 필요
- 120 ohm termination은 bus 양 끝에만 적용
- 1:1 bench에서는 PCAN 쪽과 transceiver 쪽 termination 상태를 실제 저항 측정으로 확인

중요 주의:

- Adafruit board가 VCC와 VIO를 분리해서 노출한다면 VIO는 Portenta 3.3 V에 맞춘다.
- Adafruit board가 단일 VIN/VCC만 노출한다면 해당 보드가 3.3 V 동작용인지 schematic으로 확인한다.
- Portenta RXD 입력에 5 V가 들어오면 안 된다.
- 비절연 TJA1051 breakout이면 차량 field ground와 Portenta/USB ground가 사실상 연결되는 구조가 된다. 산업용/차량 장착 최종형에서는 isolated CAN transceiver 또는 절연 carrier가 더 안전하다.

---

## 12. 작업 Phase 제안

### Phase 0. Datasheet/pinout 확정

반드시 확인할 문서:

1. Arduino Portenta H7 full pinout
2. Arduino Portenta H7 schematic
3. Arduino Portenta Mid Carrier ASX00055 schematic/pinout
4. Adafruit ada-5708 schematic
5. TJA1051T/3 datasheet
6. STM32H747 alternate function table / FDCAN pin mux

산출물:

- `board/docs/HARDWARE_MID_TJA1051_DUAL_CAN_KO.md`
- 확정 CAN lane A/B pin table
- 전원/VIO/VCC/STB/termination 배선표

### Phase 1. 문서/하네스 정합성

- root/board BRIEF에서 기존 MCP production 표현을 legacy로 내림
- `board/BRIEF.md` current baseline을 새 hardware target과 verified old baseline으로 분리
- `shared/docs/TRANSPORT_AND_RECORDS_KO.md`에 CAPABILITY v2 계획 추가
- `board/docs/HARDWARE_FINAL_CONCEPT_KO.md`에서 MCP는 old bench, Mid+TJA1051은 next hardware로 분리

### Phase 2. Firmware compile profile 추가

- `portenta_h7_m7_mid_dual_fdcan` env 추가
- MCP env는 legacy/bench로 유지
- `BOARD_ENABLE_MCP2515=0` production default 확정
- 아직 HAL dual FDCAN 구현 전이면 compile stub로 시작

### Phase 3. CanLane 분리

- MCP 코드와 built-in CAN 코드를 lane class/function으로 분리
- 기존 동작 유지
- `main.cpp` orchestration만 남기는 방향으로 축소

### Phase 4. Dual FDCAN backend 구현

- Arduino_CAN dual instance 가능 여부 확인
- 불가 시 STM32 HAL FDCAN1/FDCAN2 backend 작성
- bus0/bus1 RX/TX counters 분리
- per-bus CAN_TX_RAW audit 분리

### Phase 5. Capability/Health v2

- CAPABILITY 확장
- Qt parser 호환성 확보
- BoardHealth per-bus 확장

### Phase 6. Qt/VMS 반영

- bus label 하드코딩 제거
- MCP label은 legacy profile로만 표시
- capability-driven bus role 표시
- control bus allowlist UI/logic 정렬

### Phase 7. HIL 검증

- bus0 PCAN RX smoke
- bus1 PCAN TX/RX smoke
- dual bus simultaneous RX flood
- host command 20 ms 제어 burst
- no ACK 상태 TX fail 표시
- termination on/off 확인
- standby/safety gate 확인

---

## 13. Codex 플랜모드 지시문

```text
[PLAN MODE ONLY]

이번 작업은 CSM 코드 수정이 아니라 하드웨어 변경 반영 계획 수립 턴이다.

현재 csm(2) 폴더의 AGENTS.md, BRIEF.md, board/AGENTS.md, board/BRIEF.md, shared/docs/TRANSPORT_AND_RECORDS_KO.md, board/docs/HARDWARE_FINAL_CONCEPT_KO.md, include/BoardPins.h, platformio.ini, src/main.cpp를 먼저 읽어라.

그 다음 새 하드웨어 변경을 반영하라.

기존:
- Portenta H7 + HAT Carrier
- MCP2515 + TJA1050 외부 CAN controller/transceiver
- MCP bus=0 + built-in CAN bus=1

변경:
- Portenta H7 + Mid Carrier ASX00055
- Adafruit ada-5708 TJA1051T/3 transceiver
- MCP2515 외부 CAN controller 삭제
- Portenta H7 내부 CAN/FDCAN controller 2개 lane 사용
- 외부 보드는 controller가 아니라 transceiver만 담당

절대 바로 코드 수정하지 말고 conflict report + migration phase plan을 작성하라.

반드시 판단할 것:
1. 기존 AGENTS/BRIEF의 MCP bus=0 기준과 새 dual internal CAN 기준의 충돌
2. BoardPins.h에서 MCP SPI 핀을 production에서 내리고 새 CAN TX/RX 핀을 확정하는 방법
3. Arduino_CAN이 dual internal CAN을 지원하는지, 아니면 STM32 HAL FDCAN backend가 필요한지
4. CAPABILITY v1을 유지할지, CAPABILITY v2 per-bus descriptor가 필요한지
5. BOARD_HEALTH에서 MCP error 지표를 어떻게 per-bus FDCAN 지표로 바꿀지
6. Qt/VMS가 bus=0 MCP, bus=1 built-in으로 하드코딩한 부분을 어떻게 제거해야 하는지
7. legacy 20-byte는 replay/import compatibility로만 보존하고 live stream은 typed-only로 유지하는 방법
8. CONTROL_ACK와 CAN_TX_RAW audit 분리 원칙을 유지하는 방법
9. Adafruit TJA1051T/3 VCC/VIO/STB/termination 배선 확인이 필요한 항목
10. 실제 HIL에서 두 CAN bus를 어떤 순서로 검증할지

첫 산출물은 코드 패치가 아니라 다음을 포함한 계획이어야 한다.
- conflict report
- 문서 수정 목록
- protocol/capability 변경안
- firmware phase plan
- Qt/VMS 영향 범위
- build/test/HIL 검증 항목
- 바로 수정하면 위험한 항목
```

---

## 14. 최종 판단

이번 변경의 정답은 “MCP2515 관련 코드만 지우기”가 아니다.

정답은 다음이다.

```text
MCP2515 controller lane을 production에서 내리고,
Portenta 내부 CAN/FDCAN lane 2개를 bus0/bus1로 승격하며,
TJA1051은 물리 transceiver로만 취급하고,
CAPABILITY/BOARD_HEALTH/Qt bus 표시를 capability-driven 구조로 바꾸는 것.
```

가장 위험한 착각은 아래다.

```text
TJA1051을 MCP2515 대체 라이브러리처럼 쓰는 것
```

TJA1051은 라이브러리로 제어하는 CAN controller가 아니다.  
Portenta 내부 CAN peripheral을 제대로 열고, TXD/RXD를 transceiver에 연결해야 한다.
