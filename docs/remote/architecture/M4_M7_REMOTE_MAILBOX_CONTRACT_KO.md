# CSM Remote M4-M7 Remote Mailbox Contract

작성일: 2026-07-09
상태: Phase 2B fixed frame/writer builds on M4, IPC mechanism not closed
대상 코드:

```text
firmware/csm/include/board/remote/M4RemoteMailboxContract.h
firmware/csm/src/board/remote/M4RemoteMailboxContract.cpp
firmware/csm/include/board/remote/M4RemoteMailboxWriter.h
firmware/csm/src/board/remote/M4RemoteMailboxWriter.cpp
firmware/csm/include/board/remote/M4RemoteMailboxReader.h
firmware/csm/src/board/remote/M4RemoteMailboxReader.cpp
```

이 문서는 Portenta H7 M4 리모컨 프론트엔드가 M7 권한 판단 경로로 넘길 RC sample의
고정 binary contract를 정의한다.

이 문서가 확정하는 것:

```text
M4가 생산할 mailbox frame layout
M4가 RcSample을 frame으로 포장하는 writer helper
M7이 torn/corrupt/stale/protocol fault를 거부하는 기준
M4와 M7이 공유할 sample state 의미
```

이 문서가 아직 확정하지 않는 것:

```text
Portenta H7 실제 M4-M7 IPC 방식
shared memory 위치
cache coherency/barrier/HSEM/OpenAMP/RPC 선택
CRSF UART baud/cadence
```

따라서 OD-004는 아직 닫지 않는다.

---

## 1. Product Boundary

M4는 리모컨 수신/파싱/정규화만 담당한다.

```text
R16SM CRSF/SBUS bytes
  -> M4 parser/normalizer
  -> M4RemoteMailboxFrame
  -> M7 M4RemoteMailboxReader
  -> RemoteControlSource
```

금지:

```text
M4는 CAN ID를 알지 않는다.
M4는 CAN payload를 만들지 않는다.
M4는 authority/safety/CAN TX 판단을 하지 않는다.
M7은 CRC/torn/protocol 검증을 통과하지 않은 sample을 local source로 승격하지 않는다.
```

---

## 2. Frame Layout

Frame size는 64 bytes로 고정한다.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | `sequence_begin` | seqlock begin sequence |
| 4 | 4 | `magic` | `0x344D4352`, ASCII `RCM4` |
| 8 | 1 | `version` | `1` |
| 9 | 1 | `sample_state` | `RcSampleState` raw value |
| 10 | 2 | `frame_size` | `64` |
| 12 | 2 | `rc_seq` | RC sample sequence |
| 14 | 2 | `flags` | RC parser flags |
| 16 | 4 | `m4_time_ms` | M4 local monotonic time |
| 20 | 32 | `ch[16]` | normalized channel permille/raw contract value |
| 52 | 2 | `switch_bits` | decoded switch bitmap |
| 54 | 1 | `link_quality` | `0xFF` if unknown |
| 55 | 1 | `rssi_hint` | `0xFF` if unknown |
| 56 | 2 | `malformed_count` | M4 parser malformed frame counter |
| 58 | 2 | `crc` | CRC16-CCITT over stable payload fields |
| 60 | 4 | `sequence_end` | seqlock end sequence |

`static_assert(sizeof(M4RemoteMailboxFrame) == 64)`가 firmware build에서 이 계약을 보호한다.

---

## 3. Seqlock Rule

M7은 다음을 모두 만족할 때만 frame을 torn-free로 본다.

```text
sequence_begin != 0
sequence_begin == sequence_end
sequence_begin is even
```

권장 M4 publish 순서:

```text
1. next_even = previous_even + 2
2. sequence_begin = next_even | 1
3. payload fields write
4. crc write
5. sequence_end = next_even
6. memory barrier
7. sequence_begin = next_even
```

M7 판정:

```text
0/0 sequence -> NoFrame
mismatch sequence -> Malformed / TornWrite
odd sequence -> Malformed / TornWrite
```

실제 barrier/cache/HSEM 방식은 OD-004 closure evidence가 필요하다.

---

## 4. CRC Rule

CRC는 CRC16-CCITT, initial `0xFFFF`를 사용한다.

CRC 포함 field:

```text
magic
version
sample_state
frame_size
rc_seq
flags
m4_time_ms
ch[16]
switch_bits
link_quality
rssi_hint
malformed_count
```

CRC 제외 field:

```text
sequence_begin
crc
sequence_end
```

모든 multi-byte field는 little-endian byte order로 CRC에 투입한다.

---

## 5. State Mapping

| RcSampleState | RemoteLinkState | M7 usability |
|---|---|---|
| `Lost` | `Searching` | reject |
| `Ok` | `Valid` | usable candidate |
| `Failsafe` | `Failsafe` | reject |
| `Stale` | `Stale` | reject |
| `CrcBad` | `Malformed` | reject |
| `ProtocolFault` | `ProtocolFault` | reject |

오직 `RcSampleState::Ok`와 `RemoteLinkState::Valid`만 fresh local source 후보가 될 수 있다.

---

## 6. M7 Reader Contract

`M4RemoteMailboxReader::updateFromMailboxFrame()`은 다음 순서로 거부한다.

```text
NoFrame
TornWrite
BadMagic
BadVersion
BadFrameSize
BadSampleState
CrcMismatch
SampleNotUsable
Stale timeout
```

거부된 frame은 `RemoteControlSource`에서 `OperatorCommand`로 승격되지 않는다.

---

## 7. M4 Writer Helper Contract

`M4RemoteMailboxWriter::publishSample()`은 다음만 수행한다.

```text
RcSample magic/version/state 검증
normalized channel range 검증
M4RemoteMailboxFrame payload fill
CRC16-CCITT 계산
seqlock begin/end sequence fill
```

금지:

```text
shared memory address ownership
cache barrier
HSEM/OpenAMP/RPC call
UART access
CAN ID/payload
authority/safety/CAN TX judgment
```

따라서 writer helper가 생겼다는 사실은 OD-004 closure가 아니다.

---

## 8. Evidence

Phase 1D evidence:

```text
M4RemoteMailboxContract.cpp compiles in passive product env.
M4RemoteMailboxFrame is fixed at 64 bytes.
M7 reader can decode frame internally instead of trusting external integrity_ok.
No M4 build env, real IPC, CRSF parser, or CAN TX path was added.
```

Phase 1F evidence:

```text
M4RemoteMailboxWriter.cpp compiles in passive product env.
Writer can produce a valid 64-byte frame from RcSample without owning actual IPC.
No M4 build env, real IPC, UART, or CAN TX path was added.
```

Phase 1G evidence:

```text
RemoteContractSelfTest.cpp compiles a synthetic writer/reader roundtrip helper.
It does not own shared memory placement, cache barrier, HSEM/OpenAMP/RPC, or torn-read bench evidence.
```

Phase 2A evidence:

```text
M4RemoteMailboxWriter and M4RemoteMailboxContract compile in
portenta_h7_m4_remote_frontend_build_proof.
This proves M4 target compatibility for the writer/contract only.
```

Phase 2B evidence:

```text
M4RemoteMailboxWriter and M4RemoteMailboxContract compile in
portenta_h7_m4_remote_serial3_capture_probe.
The probe writes only a local frame; it does not select shared memory placement,
cache barrier, HSEM/OpenAMP/RPC, or torn-read/stale bench behavior.
```

Phase 2C software evidence:

```text
RemoteContractSelfTest.cpp now checks synthetic happy-path roundtrip plus
CRSF CRC reject, mailbox torn-write reject, stale timeout reject, and failsafe
sample rejection.
This is software contract evidence only; it does not replace dual-core shared
memory placement, cache barrier, HSEM/OpenAMP/RPC, or hardware torn-read bench evidence.
```
