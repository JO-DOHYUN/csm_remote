# Phase 2B R16SM Serial3 Capture Runbook

작성일: 2026-07-09
상태: Bench execution pending
대상:

```text
Portenta H7 M4
Mid Carrier
Radiolink T16D
Radiolink R16SM
firmware/csm env: portenta_h7_m4_remote_serial3_capture_probe
```

이 문서는 R16SM CRSF 신호를 CSM Remote 제품 경로에 연결하기 전,
Serial3 수신과 CRSF decode evidence를 수집하는 절차를 정의한다.

---

## 1. Boundary

이 runbook에서 허용:

```text
M4 lab-only firmware build/upload
R16SM signal electrical capture
Serial3 RX byte capture
CRSF valid frame decode evidence
captured byte replay into parser test vector
```

이 runbook에서 금지:

```text
M7 product runtime remote authority enablement
M4-M7 shared memory product IPC
vehicle CAN TX
VehicleCommandMapper real mapping
CanTxGateway backend write
VSM raw CAN command path
```

---

## 2. Build Proof

Workspace root에서 실행:

```powershell
python firmware/csm/tools/remote_phase1_guard.py
python firmware/csm/tools/remote_phase2a_guard.py
python firmware/csm/tools/remote_phase2b_guard.py
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m4_remote_serial3_capture_probe
```

현재 확인된 사실:

```text
portenta_h7_m4_remote_serial3_capture_probe build succeeded.
Default UART setting is 420000 8N1.
Serial3 compile binding is available on the Portenta H7 M4 PlatformIO target.
```

닫히지 않은 사실:

```text
R16SM actual baud
R16SM output polarity/inversion
R16SM output voltage level
Mid Carrier connector pin assignment for Serial3 RX
actual CRSF frame cadence
valid hardware decode evidence
```

---

## 3. Physical Capture Checklist

기록해야 하는 항목:

```text
T16D model/profile name
R16SM output mode
R16SM LED/mode state
R16SM signal wire color and connector pin
Mid Carrier connector/pin name
Portenta framework pin name, expected Serial3 RX = PJ_9
logic analyzer model and sample rate
measured idle level
measured voltage high/low
baud estimate
inversion/polarity result
frame period/cadence
```

주의:

```text
`SERIAL3_RX = PJ_9` is a framework-level pin name from the Portenta H7 variant.
It is not by itself proof of the Mid Carrier physical connector pin.
The connector pin must be verified against the carrier pinout and the measured signal.
```

---

## 4. Firmware Probe Behavior

`m4_remote_serial3_capture_probe.cpp` does:

```text
Serial3.begin(420000, SERIAL_8N1)
Serial3.available/read polling
CrsfParser::ingest(byte)
decodeCrsfRcChannelsPacked(frame)
RcNormalizer::normalizeCrsfChannels(...)
M4RemoteMailboxWriter::publishSample(...) into a local frame
volatile counter update for debugger/watch evidence
```

It does not:

```text
print debug logs through another serial port
write M4-M7 shared memory
call HSEM/OpenAMP/RPC
touch authority modules
touch CAN modules
write vehicle commands
```

Debugger/watch symbols:

```text
g_remote_probe_bytes
g_remote_probe_frames
g_remote_probe_rc_frames
g_remote_probe_mailbox_publishes
g_remote_probe_rejected_length
g_remote_probe_rejected_crc
g_remote_probe_last_byte_ms
g_remote_probe_last_frame_ms
g_remote_probe_last_mailbox_sequence
g_remote_probe_last_ch0
g_remote_probe_last_ch1
g_remote_probe_last_ch2
g_remote_probe_last_ch3
```

---

## 5. Evidence Acceptance

OD-003 can move from EvidenceRequired to ReadyForDecision only when all are true:

```text
logic analyzer capture file exists
capture shows stable electrical levels and polarity
baud is measured, not guessed
frame cadence is measured
captured bytes include CRSF frames with valid CRC
captured RC_CHANNELS_PACKED frames decode into 16 channels
same captured bytes pass the local parser replay test
T16D channel/switch movement changes the expected decoded channels
```

OD-003 remains open if only this exists:

```text
PlatformIO build success
Serial3 compile success
synthetic CRSF frame success
receiver LED only
manual claim of baud/mode without capture
```

---

## 6. Next Artifact

After bench capture, add:

```text
captured byte fixture under a non-generated test/evidence path
parser replay command
capture metadata document
OPEN_DECISIONS_KO.md OD-003 update
REQUIREMENT_TRACE_KO.md evidence update
CHANGE_HISTORY_KO.md entry
```
