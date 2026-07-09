# CSM Remote CRSF Frontend Contract

작성일: 2026-07-09
상태: Phase 1E parser/normalizer skeleton, UART binding not closed
대상 코드:

```text
firmware/csm/include/board/remote/CrsfParser.h
firmware/csm/src/board/remote/CrsfParser.cpp
firmware/csm/include/board/remote/RcNormalizer.h
firmware/csm/src/board/remote/RcNormalizer.cpp
```

이 문서는 R16SM CRSF byte stream을 M4 remote frontend가 어떻게 안전하게 해석해야 하는지의
platform-independent contract를 정의한다.

이 문서가 확정하는 것:

```text
CRSF frame size bound
CRSF frame length/CRC reject rule
RC_CHANNELS_PACKED 0x16 channel unpack rule
raw CRSF channel -> normalized RcSample 변환 경계
```

이 문서가 아직 확정하지 않는 것:

```text
R16SM 실제 CRSF baud
Serial3/UART binding
M4 PlatformIO env
T16D channel/switch assignment
takeover/release switch semantics
SBUS fallback 여부
```

따라서 OD-003과 OD-006은 아직 닫지 않는다.

---

## 1. Boundary

허용:

```text
byte stream -> bounded CRSF frame
CRSF frame -> raw 16ch channels
raw channels -> normalized RcSample
```

금지:

```text
UART/Serial3 직접 접근
M4-M7 shared memory write
CAN ID/payload 생성
authority/safety/CAN TX 판단
takeover/release switch 확정
```

---

## 2. CRSF Frame Contract

Frame 구조:

```text
address
length
type
payload
crc
```

Length field는 `type + payload + crc` byte 수로 취급한다.

Bounds:

```text
max total frame bytes = 64
min length field = 2
max length field = 62
max payload bytes = 60
```

Reject:

```text
length < 2
length > 62
CRC mismatch
```

CRC:

```text
CRC8 DVB-S2 polynomial 0xD5
initial 0
input range = type + payload
```

---

## 3. RC Channels Packed

지원하는 RC frame:

```text
type = 0x16 RC_CHANNELS_PACKED
payload length = 22
channel count = 16
bits per channel = 11
bit order = little-endian packed
```

다른 CRSF frame type은 현재 RC command source로 승격하지 않는다.

---

## 4. Normalization

기본 raw calibration:

```text
min = 172
mid = 992
max = 1811
```

정규화:

```text
raw_min -> -1000
raw_mid -> 0
raw_max -> +1000
```

현재 normalizer는 16채널 전부를 `RcSample.ch[16]`에 넣는다.
채널별 의미는 아직 부여하지 않는다.

Reject:

```text
normalizer not configured
bad calibration
channel count != 16
raw outside configured min/max
```

---

## 5. Evidence

Phase 1E evidence:

```text
CrsfParser.cpp and RcNormalizer.cpp compile in passive product env.
No UART, Serial3, M4 env, M4-M7 IPC, CAN TX, or vehicle mapping was added.
```

---

## 6. External References

```text
TBS CRSF spec: https://github.com/tbs-fpv/tbs-crsf-spec/blob/main/crsf.md
Betaflight CRSF implementation: https://github.com/betaflight/betaflight/blob/master/src/main/rx/crsf.c
PX4 CRSF telemetry docs: https://docs.px4.io/main/en/telemetry/crsf_telemetry
ExpressLRS serial protocol setup: https://www.expresslrs.org/quick-start/receivers/configuring-fc/
```
