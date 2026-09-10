# TRANSPORT_AND_RECORDS_KO

## 2026-09-01 Host control-plane isolation contract

This section supersedes older statements that placed Service/HIL Host commands
and causal ACK consumption on the canonical telemetry TCP epoch.

- TCP `3333` remains the live-only canonical telemetry/evidence stream with the
  existing global publish identity and bounded FIFO.
- TCP `3334` is the Host control stream. It starts each connection with its own
  `STREAM_SESSION` carrying the same M7 boot identity, then carries Host
  downlink commands and `CONTROL_ACK` only. Its connection epoch and publish
  sequence are independent from `3333`.
- The control ACK queue is fixed at 16 complete records of at most 64 encoded
  bytes. Full admission, RX overflow, socket failure, peer close, or 300 ms
  without positive TX progress closes that control epoch and discards all old
  command/ACK bytes. There is no reconnect replay, retry queue, or telemetry
  fallback.
- M7 continues to publish the same `CONTROL_ACK` on canonical USB/`3333` as
  immutable evidence. Only the `3334` copy satisfies Android causal liveness;
  canonical observation delay cannot keep or revoke Host authority.
- The bounded nonblocking Wi-Fi worker services `3334` before the bulk evidence
  socket on every turn. Telemetry queue occupancy/would-block cannot consume the
  control FIFO or reorder its sequence.

## 2026-08-03 live-first transient-envelope contract

This section supersedes the queue dimensions and close-policy statements in
all older sections. The 2026-07-30 live-only/no-ACK/no-replay decision remains
active.

- The product Wi-Fi uplink is live-only. It uses 256 descriptors and 49,152
  encoded bytes. Four descriptors and 2,112 bytes are reserved for critical
  evidence, leaving a 252-record/47,040-byte normal envelope. It is a
  scheduling-jitter buffer, not a journal.
- The enabled schema is generated exactly as 131,617 B/s and 1,095 records/s.
  Queue-coverage thresholds remain exploratory until measurement, constant
  freeze, and qualification HIL complete; no design-rate headroom is assumed.
- 32,768 bytes or 192 records enters diagnostic pressure. Pressure recovers
  only at both 8,192 bytes or less and 64 records or less. High-water pressure
  changes batching/wake behavior and counters; it never closes a TCP epoch.
- A positive socket send immediately releases the accepted bytes. A completed
  record is released immediately; a partial write retains only its unsent
  suffix.
- There is no application receive/commit ACK, reclaim cursor, disconnect
  rewind, or network backlog replay.
- The first reserve/full admission miss fixes the exact loss sequence and
  closes only the Wi-Fi epoch. Wire close reason 6 retains the historical
  `QueuePressure` name but now means actual admission loss, not occupancy.
  Five seconds without positive socket progress closes as
  `TransmitNoProgress`. Either close flushes every queued/partial byte from
  that epoch and records exact loss/close evidence. RC, authority, CAN,
  canonical publication, and USB continue.
- A new TCP client receives a fresh current `STREAM_SESSION` before any
  subsequent Live record. No record from the previous TCP epoch is replayed.
- The sink is not publisher-connected until the facade has observed the
  physical socket epoch. The mailbox rejects every non-`STREAM_SESSION` offer
  until that epoch's canonical anchor is captured. `downlinkStream()` uses the
  same live-epoch predicate and the same even RX generation observed by the
  worker and facade. The worker invalidates and clears the prior RX epoch
  before a new one becomes live, so producer isolation closes command RX
  immediately instead of waiting for vendor socket close and stale downlink
  bytes cannot cross reconnect.
- Android Capture is an independent consumer. Capture open/write/fsync/storage
  failure never controls CSM admission, socket lifetime, or Live delivery.

Compatibility:

- Record `21` keeps its historical name `APP_RX_COMMIT_ACK` and ID but is
  legacy reserved. Current CSM implementations decode-ignore a valid legacy
  frame and must not change RAM ownership, permission, epoch, queue, or any
  other product state. The ID must not be reassigned.
  Its legacy payload remains exactly 16 bytes:
  `boot_session_id u64_le` at 0 and
  `last_contiguous_publish_seq u64_le` at 8.
- Record `22 LINK_RELIABILITY_DIAGNOSTIC` schema 1 is legacy decode-only for
  existing captures and tools. Current products do not publish it and must not
  reinterpret schema 1 fields.
- The exact legacy schema 1 payload remains 128 bytes:
  - `0..7 mono_us u64`; `8 schema u8`; `9 flags u8`; `10 close_reason u8`;
    `12..15 connection_epoch u32`
  - `16..55`: boot session, last accepted, highest sent, last ACKed, and
    first-not-admitted sequence as five `u64`
  - `56..87`: offered, admitted, socket-sent, reclaimed bytes as four `u64`
  - `88..111`: retained/unsent/high-water bytes and retained/unsent/high-water
    records as six `u32`
  - `112..127`: accepted ACK, rejected ACK, rewind, journal-full as four `u32`
  - flags: bit0 session active, bit1 socket connected, bit2 ACK valid,
    bit3 integrity fault, bit4 backlog replay
- Current live FIFO/drop/flush/epoch evidence is carried by the current
  `TRANSPORT_DIAGNOSTIC`, `BOARD_HEALTH`, and `BOARD_EVENT` contracts until a
  separately versioned replacement is approved.

## 2026-07-27 active wire contract

This section supersedes older CAN_RX_SEGMENT and TRANSPORT_DIAGNOSTIC layouts
below. The outer typed v1 frame and record type numbers are unchanged.

`CAN_RX_SEGMENT` schema 2:

- Common header fields remain: segment sequence `u64` at 0, first/base capture
  sequence `u64` at 8, frame count `u16` at 16, entry size at 18, flags at 19,
  dropped total `u32` at 20, FIFO overflow total `u32` at 24.
- Byte 28 is schema `2`, byte 29 is header size `40`, bytes 30..31 are zero,
  and bytes 32..39 hold base monotonic microseconds `u64`.
- Flags bit0 means capture sequence valid; bit1 means compact delta entries.
- Each 20-byte entry is: capture delta `u16` at 0, mono-us delta `u32` at 2,
  CAN id/flags `u32` at 6, DLC/flags at 10, bus at 11, data[8] at 12.
- Maximum frame count is 23. Maximum payload is 500 bytes and maximum full
  typed frame is 511 bytes.
- The header base monotonic time is the minimum timestamp in the segment;
  entries remain in capture-sequence order and may therefore have non-monotonic
  timestamp deltas. A capture delta or total timestamp span that cannot fit
  causes a segment flush before that item; values are never truncated.
  Producers merge bus queues by the smallest capture sequence.
- Legacy schema 0 (32-byte header, 30-byte entry) remains decode-only for old
  captures. Schema 2 is the only current publication. Unknown/inconsistent
  schema, header, entry size, count, DLC, or length is a visible parser failure.
- `CAPABILITY` profile minor is 1. Bytes 104..107 advertise segment
  schema/header/entry/max; capability_v3_flags bit1 advertises compact segment
  publication.

`TRANSPORT_DIAGNOSTIC` schema 4 is exactly 192 bytes:

- `0..7 mono_us u64`; `8 schema=4 u8`; `9 flags u8`; `10 close_reason u8`;
  `11 runtime_mode u8`; `12..15 connection_epoch u32`
- `16..19 offered_bytes u32`; `20..23 accepted_bytes u32`;
  `24..27 accepted_records u32`; `28..31 rejected_records u32`
- `32..35 pending_queue_bytes u32`; `36..39 pending_queue_records u32`;
  `40..43 queue_high_water_bytes u32`; `44..47 oldest_queue_age_ms u32`
- `48..51 positive_socket_bytes u32`; `52..55 completed_socket_records u32`;
  `56..59 positive_writes u32`; `60..63 would_block u32`
- `64..67 socket_errors u32`; `68..71 max_send_call_us u32`;
  `72..75 max_no_progress_ms u32`; `76..79 stall_closes u32`;
  `80..83 queue_pressure_closes u32`;
  `84..87 queue_high_water_records u32`
- `88..91 aborted_accepted_bytes u32`;
  `92..95 aborted_accepted_records u32`
- `96..103 first_lost_publish_seq u64`;
  `104..111 last_lost_publish_seq u64`
- `112..119 last_accepted_publish_seq u64`;
  `120..127 last_sent_publish_seq u64`
- `128..131 last_network_error i32`; `132 last_failure_phase u8`;
  `133..135 reserved`; `136..139 last_failure_result i32`
- `140 current_call_phase u8`; `141 current_call_flags u8` (bit0 coherent,
  bit1 in progress); `142..143 reserved`; `144..147 current_call_sequence u32`;
  `148..151 current_call_started_ms u32`; `152..155 current_call_duration_us u32`;
  `156..159 current_call_result i32`; `160..163 worker_heartbeat_age_ms u32`
- `164..167 control_connection_epoch u32`; `168 control_flags u8` (bit0
  control client connected); `169 configured_socket_max u8`; `170
  configured_tcp_socket_max u8`; `171 configured_tcp_server_max u8`; `172
  required_application_sockets u8`; `173 required_total_socket_arena u8`
  (cold HIL에서 확인된 AP 내부 1 + application 4); `174..175 reserved`
- Service/HIL 전용 `176..179 socket_arena_capacity u32`; `180..183
  socket_arena_used u32`; `184..187 socket_arena_high_water u32`; `188..191
  socket_arena_allocation_failures u32`. 이 값은 pinned lwIP arena의 bounded
  evidence이며 runtime gate나 복구 입력으로 사용하지 않는다.
- Bytes `128..191` and this record's publication belong to the compile-time
  `BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY` package. They are evidence only:
  no value is consumed by authority, freshness, safe-wire, or physical TX.
  Disabling that Service/HIL flag removes the live publication and failure
  latch; the production admission medium block remains 128 bytes.
- `last_failure_*` latches the latest actionable non-`WOULD_BLOCK` call error.
  Normal nonblocking accept polls therefore cannot erase a configure/start/
  send/receive/close failure before the 1 Hz sample. `current_call_*` remains
  the separate live/last call snapshot used to diagnose an in-progress stall.
- Flag bit4 means the first/last loss sequence range is valid. A fresh anchor
  never clears this cumulative range.
- Flag bit0 is enabled, bit1 connected, bit2 socket backpressure, bit3 either
  active hysteretic high-water pressure or a pending actual-admission-loss
  close latch, and bit4 valid loss range. `queue_pressure_closes` distinguishes
  an actual reason-6 loss close from a recoverable bit3 pressure interval.
- `accepted_records` includes the dedicated current-epoch `STREAM_SESSION`.
  `rejected_records` counts offers that were never admitted, including
  pre-anchor/order violation, Busy, reserve, and full rejection.
  `aborted_accepted_*` counts accepted data discarded when its live epoch
  closes; the unsent suffix of a partially sent record counts as one aborted
  record.
- Modulo the `u32` counter width, both conservation equations must hold:
  `accepted_bytes = positive_socket_bytes + pending_queue_bytes +
  aborted_accepted_bytes`, and
  `accepted_records = completed_socket_records + pending_queue_records +
  aborted_accepted_records`.
  Total live loss is `rejected_records + aborted_accepted_records`.
- `accepted_*`, `positive_socket_*`, `aborted_accepted_*`, and
  `pending_queue_*` in one record come from one coherent worker publication.
  `pending_queue_*` is the accepted-but-not-yet-settled ledger derived as
  accepted minus sent minus aborted; it is not an independently sampled
  instantaneous FIFO depth. High-water and oldest-age remain physical queue
  observations and are not used in the conservation equation.
- Internal accepted, socket-sent, and aborted byte ledgers are `u64`; the
  schema-4 `u32` byte fields carry their low 32 bits and receivers compute
  bounded-window deltas modulo `2^32`. This keeps 24-hour operation correct
  after the first wire-counter wrap.
- Offset 108 is the upper half of `last_lost_publish_seq`; schema 4 carries no
  worker-stack field. Schema 1/2 remain decode-only. A measurement window must
  not mix schemas.

## 2026-04-22 canonical v1 addendum

This ASCII addendum is the active contract for the current board/Qt work. Older
Korean text below is retained as context, but implementations should follow this
section when there is a conflict.

Transport frame:
- SOF: `0xA5 0x5A`
- header: `version u8`, `record_type u8`, `flags u8`, `seq u16_le`, `payload_len u16_le`
- payload: record-specific little-endian binary
- trailer: `crc16_ccitt_le`
- CRC range: from `version` through final payload byte, excluding SOF and trailer
- receiver recovery: scan SOF, validate length, validate CRC, then dispatch

2026-07-15 canonical publisher rule:
- The v1 frame layout is preserved for Windows VSM compatibility.
- `seq u16` is assigned by `CanonicalPublisher` before USB/Wi-Fi fanout and is
  the low 16 bits of `publish_seq64`.
- Both sinks receive byte-identical encoded frames from the same publication.
- `STREAM_SESSION` anchors `boot_session_id` and the full `publish_seq64` at
  boot, sink epoch changes, and every low-16 sequence wrap.
- A receiver must not infer board reboot from `seq u16` alone.
- Sink queue loss remains sink-local. Source admission loss, canonical publish,
  and sink delivery counters are separate evidence.

Record types:
- `1 CAN_RX_RAW`
- `2 CAN_TX_RAW`
- `3 ENC_EDGE_RAW`
- `4 ENC_DERIVED`
- `5 ADC_SAMPLE`
- `6 CONTROL_ACK`
- `7 BOARD_EVENT`
- `8 BOARD_HEALTH`
- `9 CAPABILITY`
- `10 HOST_CAN_TX_REQUEST` legacy decode-only; never active control admission
- `11 HOST_HEARTBEAT` retired/reserved; not admitted by current runtime
- `12 HOST_CONTROL_SESSION` ARM/DISARM transaction downlink only
- `13 HOST_SET_CONTROL_POLICY` host-to-board downlink only, reserved
- `14 HOST_QUERY_CAPABILITY` host-to-board downlink only
- `16 CAN_RX_SEGMENT`
- `17 STREAM_SESSION`
- `18 REMOTE_CONTROL_STATE`
- `19 RUNTIME_DIAGNOSTIC` debug profile uplink only
- `20 TRANSPORT_DIAGNOSTIC` Wi-Fi-enabled product/debug uplink
- `21 APP_RX_COMMIT_ACK` legacy reserved; current board decode-ignore only
- `22 LINK_RELIABILITY_DIAGNOSTIC` legacy schema 1 decode-only; not currently
  published
- `23 CONTROL_TX_EVIDENCE` terminal Service/HIL command-to-driver evidence
- `24 HOST_CONTROL_STATE_V2` retired TCP state; not admitted by current runtime
- `25 HOST_CONTROL_NSHOT` generic successful-TX budget, downlink only
- `26 CONTROL_ISLAND_HEALTH` M4 terminal/transport/IPC evidence
- `28 HOST_REALTIME_STATE_V1` UDP `3335` latest Host 3-lane state, downlink only
- `29 REALTIME_PROOF_V1` UDP `3335` cumulative receiver proof, uplink only

Maximum payload length is `512` bytes for the current CSM rebuild. Hosts must
parse by `payload_len` and skip unknown trailing bytes.

`STREAM_SESSION` payload, 32 bytes:
- `0 schema_version u8`, currently `1`
- `1 reason u8`: `1 boot`, `2 sink epoch changed`, `3 sequence wrap`,
  `4 periodic`
- `2..3 flags u16`, bit0 means the boot identity used hardware TRNG
- `4 transport_version u8`, currently `1`
- `5..7 reserved`
- `8..15 boot_session_id u64`
- `16..23 publish_seq64 u64`: full identity of this `STREAM_SESSION` frame
- `24..31 mono_us u64`

`STREAM_SESSION` is critical evidence. On reconnect, a host waits for a valid
fresh current session anchor before accepting the new Live segment. The
previous TCP epoch is closed as continuous history; the product Wi-Fi sink does
not replay its unacknowledged interval. USB keeps its independent sink
behavior.

`TRANSPORT_DIAGNOSTIC` schema 1 payload is exactly 128 bytes and is emitted at
1 Hz only while an uplink host session is open. It is one low-priority
diagnostic record and never an event-per-call log. It always enters the bounded
diagnostic admission lane so existing backlog cannot hide its own cause; actual
lane/pool exhaustion remains an explicit admission failure:

- `0..7 mono_us u64`, `8 schema u8`, `9 flags u8` (`enabled`, `connected`,
  `backpressure`, `queue-pressure latched`), `10 close_reason u8`,
  `11 runtime_mode u8`, `12..15 connection_epoch u32`
- `16..31` offered bytes, accepted bytes, disconnected offers, and overflow
  as cumulative `u32`
- `32..47` current queue bytes/records, byte high-water, and oldest queued age
  as `u32`
- `48..83` socket bytes/frames, positive writes, would-block, socket errors,
  maximum send-call/no-progress time, stall closes, and queue-pressure closes
  as cumulative or maximum `u32`
- `84..103` total/TX/socket/fallback wake counters and `sigio` callbacks as
  `u32`
- `104..111` worker heartbeat age and free stack as `u32`
- `112..119 last_accepted_publish_seq u64`, `120..127
  last_sent_publish_seq u64`

Offer→accepted→socket deltas identify the first losing boundary without adding
another queue. A stable gate requires zero disconnect/overflow/socket/stall/
queue-pressure-close delta, positive socket progress, and no sustained backlog
growth beyond one 4 KiB pump budget.

`REMOTE_CONTROL_STATE` schema 4 payload, 232 bytes:
- `0..7 mono_us u64`
- `8 schema u8`, currently `4`
- `9 remote_link_state u8`, `10..11 reserved`
- `12 flags u8`: bit0 configured, bit1 M4 frontend alive, bit2 RC boundary
  reserved, bit3 usable RC sample, bit4 candidate image valid, bits5..7 reserved
- `13 link_quality u8`, `14 RSSI dBm magnitude u8`, `15 last CRSF type u8`
- `16..19 m4_boot_id u32`, `20..23 shared_sequence u32`
- `24..27 mailbox_age_ms u32`: age of the last accepted M4 mailbox sequence;
  this is IPC freshness, not RC channel-frame freshness
- `28..29 CH4 drive permille i16`, `30..31 CH2 steering permille i16`
- `32..33 raw CH2 u16`, `34..35 raw CH4 u16`
- `36..39 uart_baud u32`, `40..83` CRSF parser, mailbox, and telemetry
  counters in this order: RX bytes, valid frames, decoded RC frames, link frames,
  rejected length, rejected CRC, inter-byte reset, mailbox publish, telemetry
  frames, telemetry bytes, serial write failures
- `84..91 candidate_updates/candidate_rejects u32`
- `92..103 rejected_address/malformed_total/admission_resets u32`.
  `rejected_address` retains its specific parser meaning; `malformed_total` is
  the saturating aggregate of address, length, CRC and inter-byte reset counts.
- `104..107 ipc_rejects u32`
- `108 last_decision u8`, `109 last CRSF address u8`, `110 sample_state u8`
- `111 last_ipc_reject_detail u8`: `0` none, `1` bad shared header, `2` torn
  commit, `3` checksum mismatch
- `112..113 semantic_period_ms u16`
- `114..115 channel_valid_mask u16`
- `116..117 admission_reject_detail u16`: receiver-admission truth, independent
  from normalization detail
- `118 admission_streak u8`, `119 receiver_qualified u8`
- `120..127` max forward RPM, max reverse RPM, max steering deci-degree and
  policy id as `u16` fields
- `128..159 normalized channel[16] i16`
- `160 link_statistics_valid u8`; `161..169` exact CRSF uplink/downlink RSSI,
  SNR, antenna, RF profile, RF power, and downlink link quality fields
- `170..173 shared_publish_failures u32`: M4-to-M7 shared-window publish
  failures; this is separate from M7 IPC read rejects at `104..107`
- `176..207 raw channel[16] u16`
- `208..211 accepted_rc_frames u32`
- `212..215 normalization_rejects u32`
- `216..219 last_rc_frame_age_ms u32`
- `220..223 last_link_statistics_age_ms u32`
- `224..225 last_normalize_reject_detail u16`
- `226 link_statistics_type u8`, `227 reserved`
- `228..231 foreground_budget_hits u32`: a CRSF UART backlog remained after a
  byte/time-bounded foreground drain. The next control/health services still ran.

`RUNTIME_DIAGNOSTIC` schema 2 payload is exactly 128 bytes and exists only in
explicit `*_runtime_diag` firmware profiles. Production profiles neither emit
nor advertise it. Its purpose is reset-boundary diagnosis; it is not product
telemetry and may be removed after the hardware boundary is closed.

- `0..7 mono_us u64`
- `8 schema u8`, currently `2`
- `9 phase u8`: `1 boot checkpoint`, `2 periodic`, `3 retained TX-write-before`,
  `4 retained TX-write-return`, `5 physical TX outcome`, `6 recovered snapshot`
- `10 boot_phase u8`: setup entry through first loop, in execution order
- `11 flags u8`: bit0 FDCAN handle valid, bit1 write in progress, bit2 FIFO
  enqueue accepted, bit3 matching TX buffer `TXBTO`, bit4 `TXBRP` pending,
  bit5 `TXBCF` cancelled, bit6 recovered, bit7 recovered from another build
- `12..15 attempt_sequence u32`, `16..19 can_id_flags u32`
- `20..23 write_duration_us u32`, `24..27 write_result i32`
- `28..31 latest_tx_request_mask u32` (`1`, `2`, or `4` for the three Mbed
  TX FIFO elements), `32..35 HAL state u32`,
  `36..39 HAL ErrorCode u32`
- `40..71 fdcan[8] u32`; phase 3 stores the pre-write snapshot and later phases
  store the current/post-write snapshot. Register order is
  `CCCR, PSR, ECR, TXFQS, TXBRP, TXBTO, TXBCF, IR`. These are read-only
  snapshots; diagnostics must never clear the write-one-to-clear `IR` bits.
- `72 wifi_call_phase u8`: IP configure, AP/server start, accept, client
  configure, send, receive, close, delete의 정확한 워커 호출 경계
- `73 wifi_call_flags u8`: bit0 호출 진행 중, bit1 sink 연결, bit2 워커
  heartbeat stale, bit3 워커 실행, bit4 AP/server 준비
- `76..79 wifi_call_sequence u32`, `80..83 wifi_call_started_ms u32`
- `84..87 wifi_call_duration_us u32`, `88..91 wifi_call_result i32`
- `92..95 wifi_worker_heartbeat_age_ms u32`
- `96..99 wifi_call_stall_total u32`, `100..103 wifi_connection_epoch u32`
- `104..111 boot_session_id u64`
- `112..115 runtime_stage u32`: stage, detail, retained breadcrumb low sequence
- `116..119 wifi_worker_stack_free_bytes u32`, `120..123
  wifi_worker_stack_max_used_bytes u32`; 1초 주기로 worker 자체가 측정한다.
  MCP 상태는 `BOARD_HEALTH` evidence를 사용한다.
- `124..127 firmware_build_id u32`

The retained slot is checksum-last, double-buffered in STM32H747 backup SRAM,
and explicitly D-cache cleaned. It does not share the bootloader heap. A
checksum-valid slot from a different build is emitted with bit7 set
instead of being silently discarded. `write_result > 0` proves only that Mbed
accepted the frame into the FDCAN FIFO. Actual transmission requires the same
attempt's `TXBTO`, no `TXBCF/TXBRP`, and matching external Kvaser evidence.

The M4-M7 shared-memory schema is version `4`. Product M4 and M7 artifacts must
be deployed as a pair. The fixed 1 KiB SRAM4 window at `0x38000000` replaces the
OpenAMP resource-table window, so RPC/OpenAMP is compile-time incompatible with
the remote product profiles. The header, M4-to-M7 channel, and M7-to-M4 channel
start on separate 32-byte cache-line boundaries. Each writer cleans only its own
channel so stale M7 cache lines cannot overwrite a concurrent M4 sample. M4 owns
UART/CRSF parsing and normalization only;
M7 owns authority, limiting, vehicle mapping and coherent latest source images. M4 owns
physical CAN release and terminal evidence. For the current product contract, CH4 is drive,
CH2 is steering and CH5 is the optional auxiliary three-position switch. CH2
`-1000/0/+1000` maps to standard
CAN ID `0x007`, DLC 8, byte 0 decimal `10/130/250`; bytes 1..6 are zero. CH5 is
quantized to `-1000/0/+1000`: negative writes byte 7 `0x01`, positive writes
byte 7 `0x80`, and neutral writes `0x00`. A non-neutral CH5 overrides other RC
motion targets to neutral. CH4 positive is forward and negative is reverse. Absolute
magnitude through 5% emits stop. Above 5%, the first active speed is 200 and later
speeds are rounded to 50-unit steps through 1000 in standard ID `0x005`, DLC8:
`AA 52 speed_lo speed_hi direction 00 00 00`, direction forward `0x50`/reverse `0x60`.
Active mode never carries speed `1..199`.
RC source loss and RC failsafe use only `AA 02 00 00 00 00 00 00` while the
healthy physical transport follows the frozen SAFE wire policy.
Drive is periodic at 5 ms; steering is independently periodic at 20 ms. Both pass the limiter,
and a direction reversal reaches zero before applying the opposite direction. A repeated or frozen
mailbox sequence cannot refresh source freshness.

Remote authority order is `RC remote > service host > monitoring`; this active
profile has no autonomy source. A fresh usable RC sample requires valid `0xC8`, CRC,
calibrated CH4/CH2, and three consecutive fresh RC frames, and owns only `0x005/0x007`.
Missing Link Statistics is allowed; once observed, LQ=0 or stale statistics veto RC.
Optional CH5/CH10/CH11 validity never controls the CH4/CH2 authority boundary. RC loss immediately revokes
RC ACTIVE and exposes the lower-priority source boundary.
Malformed CRSF or IPC evidence cannot remain ACTIVE.

`CAN_RX_RAW` and `CAN_TX_RAW` payload, 30 bytes:
- `0..7 mono_us u64`
- `8..11 can_id_flags u32`: bit 0..28 ID, bit 29 extended, bit 30 RTR
- `12 dlc_flags u8`: low nibble DLC
- `13 bus u8`: logical bus id. Interpret the physical backend and role from
  `CAPABILITY`; do not hard-code MCP labels in new hosts.
- `14..21 data[8]`
- `22..25 total u32`: RX or TX success counter
- `26..29 dropped_or_failed u32`

`CAN_RX_SEGMENT` payload, `32 + frame_count * 30` bytes:
- This is lossless packing for high-load receive. It is not compression,
  sampling, or summary data.
- Current production CSM limits `frame_count` to `15` so the payload is at most
  `32 + 15 * 30 = 482` bytes and the full typed transport frame is `493`
  bytes. The previous 16-frame packing produced a 512-byte payload and a full
  typed frame larger than the USB HS 512-byte packet boundary.
- Header:
  - `0..7 segment_seq64 u64`
  - `8..15 first_capture_seq64 u64`
  - `16..17 frame_count u16`
  - `18 entry_size u8`, currently `30`
  - `19 flags u8`, bit0 means `capture_seq64` is valid
  - `20..23 dropped_before_segment u32`
  - `24..27 fifo_before_segment u32`
  - `28..31 reserved u32`
- Entry, repeated `frame_count` times:
  - `0..7 capture_seq64 u64`
  - `8..15 mono_us u64`
  - `16..19 can_id_flags u32`
  - `20 dlc_flags u8`
  - `21 bus u8`
  - `22..29 data[8]`

Sequence meanings are intentionally separate:
- `capture_seq64`: CAN frame receive sequence. It increases once per captured
  CAN frame before segment packing.
- `segment_seq64`: `CAN_RX_SEGMENT` creation sequence. It increases once per
  emitted segment attempt.
- canonical `publish_seq64`: sink-independent typed publication sequence. It
  increases only when at least one sink accepts the byte-identical encoded frame.
- typed transport `seq`: low 16 bits of `publish_seq64`.

Gap interpretation:
- `capture_seq64` gap means loss around CAN receive queueing or segment
  construction.
- `segment_seq64` gap means `CAN_RX_SEGMENT` record-level loss.
- typed transport `seq` gap means the observing sink or host missed a canonical
  publication. Use the nearest `STREAM_SESSION`, source counters, and sink
  counters to locate the loss.

Current active Service/HIL board baseline:
- feeder-UART receive frames are emitted as `CAN_RX_SEGMENT` entries with `bus=0`.
- M4 FDCAN1 raw-ring observations are emitted as `CAN_RX_SEGMENT` entries with
  `bus=1`; their canonical timestamp is M7 observation time, not a cross-core
  subtraction of the M4 local timestamp.
- `CAN_RX_RAW` remains protocol-compatible for older hosts/builds, but the
  high-load dual-channel build uses `CAN_RX_SEGMENT` for live RX uplink.
- M4 `CONTROL_ISLAND_HEALTH` owns terminal TX counters/status. `CAN_TX_RAW`
  remains independent physical observer truth and is not reconstructed from
  Host ACK or payload matching.

Legacy Mid Carrier MCP2515 profile compatibility (HISTORY, not an active build route):
- Profile major `3`.
- `bus=0`: external MCP2515/TJA1050 on Mid Carrier `D7..D11` SPI pins,
  Classic CAN 2.0, `MCP_8MHZ`, `CAN_500KBPS`. The current high-load dual CSM
  build profile uses polling drain (`BOARD_CAN_IRQ_MODE=0`), 8 MHz MCP SPI, and
  `BOARD_CAN_SERIAL_DRAIN_BUDGET=512`.
- `bus=0` emits `CAN_RX_SEGMENT` entries for received frames and `CAN_TX_RAW`
  for successful host-commanded or bench-test writes.
- the retired dual-channel build additionally exposed `bus=1`: Mid Carrier J4 CAN1 through onboard U2,
  Classic CAN 2.0, 500 kbps. `bus=1` emits `CAN_RX_SEGMENT` entries and accepts
  audited allowlisted host-commanded writes.
- The previous dual internal CAN0/CAN1 + TJA1051 target is deferred in this
  repository until a second internal CAN controller backend is implemented and
  HIL-proven.

`CONTROL_ACK` payload, 28 bytes:
- `0..7 mono_us u64`
- `8..11 command_id u32`
- `12 status u8`: `0` reject, `1` accepted by the board for processing in this v1 baseline
- `13 reason u8`: `0` ok, nonzero reject/failure reason
- `14 bus u8`
- `15 dlc_flags u8`
- `16..19 can_id_flags u32`
- `20..23 counter u32`: accepted counter or request counter depending on status
- `24..27 rejected_total u32`

`CONTROL_ACK` is session/transaction admission evidence, not physical CAN
success. Realtime state acceptance is reported by `REALTIME_PROOF_V1`.
`HOST_CONTROL_NSHOT Accepted` means M4 may observe the
transaction through the next control snapshot. Actual terminal truth is M4
`TXBRP/TXBTO/TXBCF` projected through `CONTROL_ISLAND_HEALTH`; only `TXBTO`
increments successful-TX counters. Host software must never infer actual send from
ACK or from repeated payloads.

`CONTROL_TX_EVIDENCE` schema 2 payload, 40 bytes:
- `0..7 mono_us u64`
- `8..11 command_id u32`
- `12..15 submission_sequence u32`
- `16 outcome u8`: `1 Transmitted`, `2 IntentionalCancelled`,
  `3 HardwareFailure`, `4 TrackingFailure`
- `17 origin u8`
- `18 bus u8`, `19 dlc u8`
- `20..23 can_id_flags u32`, `24..31 data[8]`
- `32..35 driver_result i32`
- `36 request_mask_low u8`, `37 cancel_reason u8`,
  `38 completion_code u8`, `39 schema u8` (`2`)

Record 23 is retained for older evidence readers but is not emitted by REV.B M7.
REV.B transaction completion is record 26's transaction id/requested/completed/state.
Continuous state has no command-correlated per-release record. `CAN_TX_RAW` remains
independent observer truth and is never paired to command ID by payload guessing.
CAPABILITY advertises three dedicated physical buffers and zero Host software
retention.

Current `CONTROL_ACK` reasons:
- `0` ok
- `1` bad payload length
- `2` bad bus
- `3` unsupported frame flag such as RTR
- `4` DLC out of range
- `5` CAN ID not allowed
- `6` built-in CAN not ready
- `7` CAN write failed
- `8` bad protocol version
- `9` safety not armed
- `10` Host realtime liveness timeout
- `11` retired lease reason (reserved)
- `12` Host realtime proof required
- `13` Host realtime proof reference mismatch
- `14` replayed/non-forward Host command ID
- `16` queue full
- `17` TX busy
- `18` bus off
- `19` error passive
- `20` role unresolved
- `21` policy hash mismatch
- `22` neutral profile missing
- `23` rate limited
- `24` unsupported command
- `25` authority denied

`HOST_CAN_TX_REQUEST` record 10 is legacy decode-only and is never admitted by the
active REV.B profile.

Retired `HOST_CONTROL_STATE_V2` payload, 40 bytes, decode compatibility only:
- `0..3 command_id u32`
- `4..7 state_generation u32`, strictly newer within the Host image epoch
- `8..11 contract_id u32`, fixed `0x484E4F31` (`HNO1`)
- `12 valid_mask u8`, bits `0/1/2` must all be set
- `13..15 reserved`
- `16..23 data_0x005[8]`
- `24..31 data_0x007[8]`
- `32..39 data_0x364[8]`

`HOST_CONTROL_NSHOT` payload, 28 bytes, host-to-board:
- `0..3 command_id u32`
- `4..7 transaction_id u32`
- `8..11 payload_generation u32`
- `12..15 contract_id u32`, fixed `0x484E4F31`
- `16 lane u8`: `0=0x005`, `1=0x007`, `2=0x364`
- `17 reserved`
- `18..19 successful_tx_count u16`, `1..255`
- `20..27 data[8]`

`CONTROL_ISLAND_HEALTH` schema 5 payload, 512 bytes:
- `0..7 mono_us u64`, M7 observation time
- `8 schema u8`, `9 CSM-local ready reason u8`, `10..11 payload_len u16`
- `12..31 schema/wire/memory identity, M4 boot id, health sequence`
- `32..51 flags, M7 publish sequence/age in M4 local time, source epoch/source`
- `52..83 IPC stale/integrity, FDCAN error, raw-ring fill/high-water/drop counters`
- `84..95 transaction id/requested/completed/state/FDCAN/M7 health-reject detail`
- `96..239` three 48-byte lane accounting blocks: schedule due, policy suppressed,
  transport blocked, pending blocked, request attempt/accepted/failed, terminal
  success/cancel/cancel-race, tracking fault, value generation
- `240..359` current/first-fault/last-fault FDCAN register/HAL snapshots
- `360..371` M7 snapshot publish total/failure/max-gap
- `372..375 activation_epoch u32`: explicit ARM/fresh activation identity, separate
  from global source selection epoch
- `376..403` exact M4 source/runtime/build identity and bring-up stage/failure/detail
- `404..443` TIM4 execution and actual FDCAN timing/500 kbps calculation
- `444..479` IRQ/callback/Add/Enable/Abort/coherent-health counters
- `480..511` M4 boot request, independent coordinator/read health, health age and
  packed global selected-source/permit truth

Flags distinguish ready/clock, bus-off, error-passive,
M7 fresh, control active, TIM4 configured/ticking and tracking fault. The record
is emitted periodically even before normal M4 health exists; zero boot/health
sequence is explicit diagnostic truth, while the independent bring-up trace can
still identify the last completed or failed boot stage.

Local ready reason: `0 READY`, `1 no health`, `2 timeout contract disabled`,
`3 M7-local health stale`, `4 bus-off`, `5 error-passive`, `6 tracking fault`,
`7 clock contract`, `8 M7 source stale at M4`, `9 M4 not ready`.
This preserves the prior M7 predicate; it is not a new permission gate.
IPC/schema identity is `0x43495345`; the trace slot remains 64 bytes.
The internal final snapshot includes `target_m4_boot_id`, bound by M7 at
activation admission. Its `activation_epoch` is allocated from one M7-global,
wrap-safe monotonic Host/RC execution domain synchronized from M4 health;
Android command/authority IDs remain transaction/proof correlation and never
enter the M4 watermark. M4 accepts ACTIVE only for its own boot instance. Both
cores must migrate together; external health record offsets remain unchanged.

`CONTROL_PATH_DIAGNOSTIC` record 27, schema 2, 496 bytes, Service/HIL only:
`0..7 M7 mono_us`, `8 schema`, `9..11 reserved zero`; every remaining field is
u32 LE at its canonical `kControlPathDiagnostic*Offset` in `TypedRecords.h`.
`12..211` current local readiness, M4 health generation/publication, M7 read,
control RX/heartbeat/ACK generation-admission-socket TX, call/telemetry/arena;
`212..291` retained M7 first failure and its local/transport context;
`292..323` retained first control-transport failure; `324..339` M4 snapshot
rejects, staged TX command/offset and independent trace TIM4 counter;
`340..495` realtime RX/admission/proof and retained first-failure context,
as named by the current canonical offsets (not a high-rate packet log).
Unused/reserved fields never assert PASS. Diagnostic priority, batchable delivery,
at most one record/1000 ms; extra
Service/HIL wire budget is 507 B/s with unchanged 512-byte maximum and queues.

Debug observation record 30 is reserved by
[`observability/contract.json`](../observability/contract.json), with generated
layout/API/manifest and lifecycle in [its contract guide](../observability/README.md).
It is USB-debug/Android-local-file only, on an independent per-producer sequence
domain: never a canonical TCP3333 publication, control record or M4 IPC addition.
P1 provides codecs only; production hooks, sink demultiplexing and measured
coverage remain unimplemented. Record 27 remains the low-rate summary.

M7 first reason `1 local M4 not-ready`, `2 causal proof expired`, `0x100 +
HostControlCloseReason` for other unexpected authority closures. Explicit ARM
starts observation but never clears the retained first failure. Transport reason
`1 peer closed`, `2 RX error`, `3 RX overflow`, `4 TX error`, `5 local close`,
`6 TX no-progress`, `7 ACK queue admission loss`. First context survives
reconnection, normal DISARM and subsequent errors; boot resets it.
All `*Ms` are M7-local modulo-u32 times. ACK socket acceptance is not Android
receipt. `ControlTxPendingId/Offset` retain the last staged send including a
completed frame (compare AckSentId); RX/TX counters are independent atomic
observations, not a transactional conservation snapshot.

`REMOTE_CONTROL_STATE` schema 4 is RC frontend/candidate evidence only. It owns
CRSF/mailbox/normalization/semantic-candidate counters and no global authority,
physical cadence, request, terminal, or FDCAN success meaning.

Current active Service/HIL Host policy:
- Android owns vehicle semantics and the final three payloads but no physical
  release clock.
- M7 retains one coherent latest image only. Partial valid masks, repeated/older
  generations, wrong contract identity, inactive authority or expired session are
  rejected. There is no Host execution FIFO, per-frame request path, catch-up,
  hidden retry or replay.
- M4 releases standard DLC8 `0x005/0x007/0x364` from dedicated Tx buffers on its
  fixed 5/20/20 ms slot table. Value generation and publish liveness are separate.
- M4 transport readiness와 ACTIVE motion permission은 별개다. source/lease/ARM/M7
  freshness/permit 상실은 old ACTIVE image를 revoke하며, healthy transport에서는
  frozen lane-safe policy만 적용한다: `0x005` safe `AA 02 00 00 00 00 00 00`,
  `0x007` safe `82 00 00 00 00 00 00 00`, `0x364` `SuppressTx`.
  이는 M4 vehicle semantic 계산이나 implicit zero fallback이 아니다.
- N-shot is a source-agnostic successful-TX budget. `TXBTO` increments the count;
  Nth success immediately blocks further transaction releases.
- The Service/HIL Wi-Fi profile accepts Host control downlink only from active TCP `3334`
  client. USB CDC remains an independent observation sink and is not a second
  host-control source in that profile.
- TCP arrival spacing and Host timestamps are not CAN cadence clocks. M4 TIM4 is
  the only nominal physical request clock.
- Heartbeat, lease, authority or backend loss rejects new Host requests.
  Host-to-RC handoff is one ordered state transition: close Host admission,
  terminate Host freshness/lease epoch, request cancellation and wait for every
  admitted Host HW terminal, then allow RC. There is neither overlap nor an
  artificial wait after the final terminal. Transport/Host-authority loss requests cancellation of Host-origin HW attempts;
  FDCAN terminal fault requests cancellation of all application-control origins. Attempts
  remain journaled until transmitted/cancelled/faulted terminal truth, and new
  Host ARM waits for that closure.
- Timing/hardware facts are not inferred from tests. Qualification remains measured evidence,
  never a physical runtime permission gate.
- The Wi-Fi sink owns the accepted raw mbed `TCPSocket` directly. The accepted
  socket is nonblocking; TX, downlink RX, and close are serviced only from the
  single bounded `WifiSocketWorker`. Product firmware must not wrap the accepted
  socket in Arduino `WiFiClient`, create another socket owner, or perform vendor
  socket calls from the CAN/main loop.
- The worker is event-driven. Empty-to-nonempty producer transitions, critical
  records, control requests, and socket `sigio` set RTOS event flags. The
  callback performs no socket operation. A 5 ms connected fallback wake covers
  lost/coalesced notifications; there is no 1 ms polling loop. Positive bounded
  progress self-schedules another drain wake while data remains. Worker state is
  coalesced to 100 ms except forced connection transitions.
- One active client is allowed per listener (`3333` evidence, `3334` control).
  While one is active its own listener is not polled; an extra connection to
  that same port remains outside its stream. Disconnect or
  stall handling clears only
  that sink's queued copies and advances its connection epoch; it never clears
  source truth or another sink.
- A Service/HIL Wi-Fi sink receives `STREAM_SESSION` at its connection epoch.
  The announcement remains pending for that sink until its queue accepts it; it
  is not periodically inserted ahead of already queued canonical records. CAN RX
  uses the existing bounded 20 ms/15-frame segment builder. These are
  identity/throughput controls; Android health/remote freshness thresholds are
  not relaxed.
- Remote Product uses a 48 KiB byte pool plus 256 frame descriptors, with
  2,112 bytes plus four descriptors reserved for critical evidence. The
  32 KiB/192-record high-water only bypasses batching and exposes hysteretic
  pressure; occupancy is not a close condition. The worker isolates a client
  after 5 s continuous
  TX no-progress. Peer close, non-`WOULD_BLOCK` socket error, RX overflow, and
  explicit isolation also close only that sink. Reconnect starts a new epoch and
  reports the loss boundary; source truth and RC/CAN execution are unaffected.
- Every Wi-Fi client close is retained until an uplink sink accepts
  `BOARD_EVENT` code `45`. `detail` is `WifiCloseReason` (`1 peer`, `2 socket`,
  `3 RX overflow`, `4 TX no-progress`, `5 isolation`, `6 queue pressure`) and
  `counter` is the cumulative disconnect count. This event, not a host EOF
  message, is the authoritative close boundary.
- The TX mailbox is single-producer/single-consumer. The main publisher owns
  producer indices and the socket worker owns consumer indices; neither normal
  enqueue nor drain takes a shared queue lock. Abort ownership remains with the
  worker and uses a nonblocking producer gate. Reserved-capacity rejection or an
  actual full queue remains explicit sink-miss evidence. The first such loss
  atomically latches one `QueuePressure` disconnect request; the worker
  aborts/closes that sink epoch, and admission remains closed until main observes
  the worker epoch increment. Main/RC/CAN never waits.
  Each descriptor is a fixed 16-byte committed-frame cursor; record type is not
  duplicated and the low 16 bits of its monotonic committed byte end are stored.
  Because the byte envelope is below 65,536 bytes, unsigned 16-bit subtraction
  recovers the exact committed distance, including a completely full ring and
  32-bit counter wrap.
  Descriptor-tail release is the sole record commit, so the consumer cannot copy
  bytes that the producer has not committed. Byte occupancy is published first,
  so a concurrent health snapshot may conservatively lead by at most one
  in-flight frame but never under-reports reserved storage. Mailbox health reads
  the queue's atomic producer/consumer cursors directly; it does not maintain a
  second cached snapshot that either side could overwrite out of order.
  Descriptor and byte capacity are independent compile/link-time gates. With
  the current enabled periodic mix is 496 records/s; the previous 250 ms /
  197-record claim is invalid. Exploratory builds advertise transient coverage
  `0` and cannot qualify. A nonzero coverage interval is frozen only after
  measurement, at which point compile-time guards must prove both descriptor
  and byte capacity. Neither dimension may be enlarged to make a test pass.
- State/transaction ACK remains admission-only. M4 health and independent CAN
  observer records carry physical terminal truth.

Host control session:
- `HOST_CONTROL_SESSION` payload, 24 bytes:
  - `0..3 command_id u32`
  - `4 action u8`: `0` disarm, `1` arm; other values are reserved/unsupported
  - `5 requested_bus u8`: physical bus id or `0xFF` for any configured control backend
  - `6..7 flags u16`
  - `8..9` reserved compatibility bytes
  - `10..11 reserved u16`
  - `12..15 host_mono_ms u32`: diagnostic only
  - `16 control_schema u8`: `4`; DISARM remains fail-safe

`HOST_REALTIME_STATE_V1` is one exact 64-byte payload per UDP `3335` datagram.
It carries schema/mode, boot and activation identity, process-scoped realtime
sequence, coherent state generation, cumulative proof reference, diagnostic
host monotonic time, contract/valid mask and exact 005/007/364 bytes.

`REALTIME_PROOF_V1` is one exact 36-byte payload per UDP `3335` datagram. It
carries CSM monotonic time, boot/activation identity, proof sequence, highest
accepted realtime sequence, applied generation and status/reason/flags. Both
directions retain only the newest item; no TCP fallback, retry, replay or catch-up.
`STATE_APPLIED` means M7 accepted the state and observed matching M4 boot plus
physical ACTIVE activation; M7 staging/admission alone never asserts it.
  - `17..23 reserved`
- `HOST_QUERY_CAPABILITY` payload is either 0 bytes or `command_id u32`.
  It is the only downlink record accepted on telemetry TCP `3333`, so
  ProductionObserver can probe identity without opening Host authority. A valid
  query on either channel requests fresh canonical
  `STREAM_SESSION -> CAPABILITY` evidence on TCP `3333`/USB and returns its
  `CONTROL_ACK` on TCP `3334` (plus the canonical evidence mirror). The query is
  idempotent and gives a reconnecting host a fresh boot identity/sequence
  anchor even if it missed the connection-edge announcement.
- Service/HIL Host TX requires accepted ARM, a ready target backend and the
  receiver-local UDP bidirectional proof. PRE-ARM requires a newly issued
  challenge to return after boot/DISARM/expiry/inactive TCP epoch.
- TCP session/N-shot transactions use the wrap-safe consumed command-ID
  watermark. UDP state uses its independent process-scoped sequence domain.
  The TCP watermark resets only on M7 boot/new TCP epoch; DISARM, re-ARM,
  liveness timeout, RC preemption and state replacement do not reset it.
- UDP forward/proof-reference freshness initially expires after `350 ms`.
  Expiry closes Host authority. `host_mono_ms` is diagnostic only and is never
  compared with the CSM clock for safety. Resumed UDP cannot auto-arm.

Reserved next-phase `CONTROL_ACK` status names, without changing the current v1
payload:
- `0 REJECTED`
- `1 ACCEPTED`
- `2 ACCEPTED_WRITTEN` optional if a future queued lane wants separate written acknowledgment
- `3 ACCEPTED_RATE_LIMITED` optional

Reserved next-phase `CONTROL_ACK` reasons:
- `9` safety not armed
- `10` host timeout
- `11` rate limited
- `12` queue full
- `13` estop active
- `14` fault active

Current `BOARD_EVENT` codes used by the reference firmware:
- `1` boot
- `2` legacy MCP/CAN backend begin failed
- `3` CAN RX queue drop
- `4` encoder fault asserted
- `5` field power lost
- `6` estop asserted
- `7` encoder index
- `8` encoder wrap
- `9` MCP2515 error. This is reserved for actual MCP2515 SPI/CAN error
  evidence such as invalid all-ones status reads, `ERRIF`, `MERRF`, or RX
  overflow flags. Plain TX-complete interrupt housekeeping must not be reported
  as this error.
- `10` MCP2515 SPI snapshot
- `11` built-in CAN begin failed
- `12` built-in CAN TX failed
- `13` host frame CRC failed
- `14` host CAN TX rejected
- `15` host CAN TX accepted
- `16` Mid Carrier target CAN0 backend unavailable or pending
- `17` MCP2515 TX failed
- `18` safety state changed
- `19` host heartbeat observed
- `20` host control session decision
- `21` unsupported host command
- `22` fault lockout cleared
- `23` firmware identity boot marker. `detail low byte` is identity payload
  version, `detail high byte` is dirty flag, `counter` is firmware build id.
- `24` serial TX backpressure recovered. `detail` is duration ms for recovered
  CDC backpressure and `counter` is the serial backpressure total.
- `25` legacy uplink staging clear. Product firmware must not emit this because
  connected/disconnected typed-frame clear is forbidden.
- `26` CAN_RX_SEGMENT enqueue failed/loss accounting. `detail` is the capped
  pending CAN frame count and `counter` is `can_segment_enqueue_fail_total`.
- `27` USB CDC session opened. `detail` bit0 means CDC DTR session is required,
  bit1 means DTR is session-only/passive-safe. `counter` is the session-open
  count since boot.
- `28` USB CDC session closed. The close is reported on the next session open
  because the board cannot transmit while the CDC session is closed. `detail`
  is the previous session duration in milliseconds, saturated at `0xFFFF`.
  `counter` is the session-close count since boot.
- `29` USB CDC DTR changed. `detail` is 1 when asserted and 0 when deasserted.
- `30` USB host absent CAN discard summary. Reported on the next session open.
  `detail` is the host-absent duration in milliseconds saturated at `0xFFFF`;
  `counter` is total frames explicitly discarded while the host was absent.
- `31` MCP passive mode readback. `detail high byte` is CANCTRL and
  `detail low byte` is TXREQ bitset for TXB0/TXB1/TXB2.
- `32` MCP passive mode violation. Same detail format as `31`; this is
  product-blocking passive evidence.
- `33` MCP TXREQ violation. Same detail format as `31`; this is
  product-blocking passive evidence.
- `34` transceiver safe-state changed.
- `35` USB power or reset suspected. This is product-blocking passive evidence
  until hardware investigation clears it.
- `36` CAN front-end pre-session hold. Passive firmware has opened a host
  session but is intentionally holding MCP/built-in CAN initialization until the
  quiet window expires. `detail` is the quiet window in milliseconds.
- `37` CAN front-end session ready. Deferred CAN front-end initialization
  succeeded and ACK-observe was armed. `detail` bit0 means MCP ready, bit1 means
  built-in CAN ready, bit2 means ACK-observe enabled.
- `38` CAN front-end session init failed. Deferred CAN front-end initialization
  failed and ACK-observe remains disabled. `detail` uses the same ready bitmask.
- `39` CAN front-end fault hold. A passive readback, TXREQ, SPI all-ones, or
  deferred-init fault forced the product back to no-ACK hold. This is
  product-blocking evidence until inspected.
- `40` Wi-Fi TX backpressure close. `detail` is the continuous no-progress
  duration in milliseconds saturated at `0xFFFF`; `counter` is
  `wifi_stall_close_total`. The event is published after the blocked Wi-Fi
  client is closed, so USB can retain the exact close context and a later
  Wi-Fi epoch observes the updated health counter.
- `41` retained runtime breadcrumb recovered. The firmware writes two
  checksum-protected slots in the Portenta `.keep.uninitialized` region before
  and after external driver calls. On the next boot, a valid slot from the same
  firmware build is published once a sink connects. `detail low byte` is the
  in-progress stage, `detail high byte` is the stage substep, and `counter` is
  the previous boot uptime in milliseconds. Stages are `0 idle`, `1 USB
  connection poll`, `2 Wi-Fi connection poll`, `3 canonical publish`, `4 USB
  transmit`, `5 Wi-Fi transmit`, `6 host downlink`, `7 MCP entry drain`, `8 MCP
  interleave drain`, `9 MCP main drain`, `10 built-in CAN drain`, `11 CAN record
  drain`, `12 status/sensors`, `13 health publish`, and `14 setup`. A missing
  breadcrumb is valid evidence for memory loss or a different firmware build;
  it must not be converted into a fabricated stage.

Serial CDC uplink policy:
- Connected CDC backpressure must never clear queued/staged uplink data.
- Before a USB CDC host session is open, the board must not stage typed uplink
  records into the payload pool. In passive field builds it must also defer
  MCP/built-in CAN initialization until host session stability plus quiet time.
  Capability/health/session evidence is emitted when the session opens; new
  `CAN_RX_SEGMENT` evidence starts only after event `37`.
- Production dual CSM requests CDC sends in 512-byte chunks, with each pump
  bounded by a maximum of two write attempts, 1024 requested bytes, and a
  firmware time budget.
- `BOARD_EVENT` code `24` is not emitted repeatedly while CDC remains blocked.
  Ordinary CDC backpressure is reported as diagnostic evidence after recovery;
  CAN_RX_SEGMENT loss accounting is reported separately with code `26` and
  critical priority after the critical queue can accept it.
- Admission suppression caused by CDC backpressure or queue pressure is policy
  drop, not a serial enqueue failure. Serial enqueue failure counters are
  reserved for not-ready/no-space/hard staging failures.
- `BOARD_EVENT` code `25` is retained for historical decode compatibility only.
  Product firmware has no stale-byte clear API, so this event and the matching
  health clear counters must remain 0.
- Priority admission is applied before any record is serialized into bytes.
  The production CSM uses a fixed payload pool plus priority descriptor queues:
  critical control/fault, CAN truth, normal health/state, and diagnostic. The
  descriptor queues do not contain 512-byte payload copies.
- The large payload pool has a CAN truth reserve. Normal and diagnostic records
  cannot consume that reserve, so repeated MCP/debug/profiler traffic cannot
  block `CAN_RX_SEGMENT` storage.
- A typed frame must be atomically staged for CDC output. Implementations must
  calculate the full typed frame length, verify that the active typed-frame
  staging buffer is free, then stage all bytes or stage none.
- Partial typed frame staging is forbidden because it can corrupt VSM parser
  resynchronization.
- `typed_seq` is assigned only when a pending record is encoded into the active
  CDC staging buffer. Reordering across priority queues therefore changes only
  which pending record is sent next; the typed stream sequence remains
  contiguous in actual byte-stream order.
- CAN truth reserve users include `CAN_RX_SEGMENT` and legacy `CAN_RX_RAW`.
  Critical control/fault users include `CONTROL_ACK`, `CAN_TX_RAW`,
  fault/safety transitions, and explicit loss accounting.
- Diagnostic records include debug, profiler, repeated MCP status/error, and
  verbose log events.
- The firmware may suppress diagnostic records during the boot quiet window or
  while CDC TX pressure is active. Critical evidence, `CAPABILITY`, and
  `BOARD_HEALTH` must remain admissible.
- Low-value periodic evidence such as encoder-derived records may be sampled at
  a lower rate while CDC TX pressure is active. This must not affect raw CAN
  segment evidence or control acknowledgements.

`ADC_SAMPLE` payload, 44 bytes:
- `0..7 mono_us u64`
- `8..11 sample_total u32`
- `12..15 dropped_total u32`
- `16 source_id u8`: `0` Portenta lab ADC, later values may describe external ADC front-ends
- `17 channel_count u8`: maximum 8
- `18 resolution_bits u8`
- `19 flags u8`: bit0 raw valid, bit1 direct MCU ADC, bit6 saturated, bit7 read error
- `20..27 channel_id[8]`
- `28..43 raw_u16[8]`

Current voltage raw channel assignment:
- `channel_id 0`: Portenta `A0`
- `channel_id 1`: Portenta `A1`
- `channel_id 2`: Portenta `A6`
- `channel_id 3`: Portenta `A7`
- `A2/A3/A4/A5` are not used in the MCP bench because they overlap the `PC2/PC3` SPI pin family.

Qt storage rule:
- Store the original typed byte stream append-only. CAN, voltage, encoder, health, and events remain in one ordered binary stream.
- Indexes, decoded values, graphs, and operator notes are derived sidecars and must not replace the original stream.
- Board direct voltage samples must not be encoded as fake CAN frames.

`CAPABILITY` minimum meaning:
- A valid typed board should emit `CAPABILITY` after boot and after host reconnect
  if the implementation supports reconnect handling.
- Host software must use `CAPABILITY` to distinguish physical serial open from a
  typed board with matching protocol.
- CSM must not hide lane differences. The profile must expose bus role,
  physical backend, transceiver, RX/TX support, host TX policy, ADC lane
  presence, safety/control support level, and known API limitations such as
  unavailable built-in CAN FIFO/bus-off counters.
- When payload extensions are needed, keep record type `9` and version/profile
  fields explicit so older hosts can reject or degrade cleanly.
- Bus descriptor byte `1` is now `role_hint` only. The value is not
  authoritative and may be `0`. VSM must bind System/Drive semantics from its
  model pack, fingerprint, or operator override, not from the physical bus id.

Current 36-byte `CAPABILITY` payload interpretation:
- `0..7 mono_us u64`
- `8 protocol_version u8`
- `9 board_profile_major u8`
- `10 board_profile_minor u8`
- `11 mono64_unit u8`: `1` microsecond
- `12..15 can_queue_size u32`
- `16..19 encoder_ppr u32`
- `20..23 encoder_channel_frequency_limit_hz u32`
- `24 CAN_RX_RAW supported u8`
- `25 CAN_TX_RAW supported u8`
- `26 ENC_EDGE_RAW supported u8`
- `27 ENC_DERIVED supported u8`
- `28 ADC_SAMPLE supported u8`
- `29 BOARD_HEALTH supported u8`
- `30 BOARD_EVENT supported u8`
- `31 ADC channel count u8`
- `32 ADC resolution bits u8`
- `33 ADC sample period ms low byte u8`
- `34 lane capability flags`: bit0 MCP bus0 RX compiled, bit1 built-in bus1 RX
  compiled, bit2 built-in bus1 TX compiled, bit3 host TX policy active on bus1,
  bit4 MCP INT_N level hint compiled, bit5 lab ADC evidence lane compiled, bit6
  MCP bus0 TX compiled, bit7 host TX policy active on bus0
- `35 limitation/policy flags`: bit0 built-in CAN detailed bus-state counters are
  not exposed by the current API, bit1 host TX allowlist active, bit2 bench
  example TX active

Extended 80-byte `CAPABILITY` payload for board profile major `2` or `3`:
- `0..35`: same prefix as the current 36-byte payload.
- `36 bus_count u8`: current Mid Carrier profile uses `2`.
- `37 descriptor_size u8`: currently `20`.
- `38..39 capability_v2_flags u16_le`: bit0 bus descriptors present, bit1
  production target, bit2 lane0 backend pending.
- `40..59 bus descriptor 0`
- `60..79 bus descriptor 1`

Each 20-byte bus descriptor:
- `0 bus_id u8`
- `1 role u8`: `1` monitor/system, `2` drive/control, `3` debug/legacy
- `2 physical_backend u8`: `1` MCP2515 legacy, `2` Arduino CAN single object,
  `3` STM32 HAL internal CAN, `4` unavailable/pending
- `3 transceiver u8`: `1` TJA1050 legacy, `2` ADA-5708 TJA1051T/3, `3` Mid
  Carrier onboard U2, `255` unknown
- `4 rx_supported u8`
- `5 tx_supported u8`
- `6 control_tx_allowed u8`
- `7 classic_can_supported u8`
- `8 can_fd_supported u8`: hardware capability only; live v1 still sends
  classic CAN payloads with `data[8]`
- `9 max_dlc u8`: currently `8` for live v1
- `10..13 nominal_bitrate u32`: current target `500000`
- `14..17 data_bitrate u32`: `0` for the Classic CAN 2.0 profile
- `18 termination_policy u8`: `0` unknown, `1` off/default, `2` selectable,
  `3` on for bench endpoint
- `19 isolation_policy u8`: `0` unknown, `1` non-isolated bench, `2` carrier or
  external isolation required, `3` isolated production path

Mid Carrier profile major `2` descriptor defaults before the dual internal CAN
backend is
implemented:
- bus0 descriptor may advertise physical target and role while setting
  `rx_supported=0`, `tx_supported=0`, and physical_backend `4` if the firmware
  cannot yet open CAN0. This is not a supported live lane until HIL proves it.
- bus1 descriptor may advertise Arduino CAN single-object backend for the
  current Portenta `PH13/PB8` path through Mid Carrier J4.
- Hosts must tolerate longer `CAPABILITY` payloads by parsing the common prefix
  and skipping unknown trailing fields.

Extended 112-byte `CAPABILITY` payload:
- `0..79`: same as the 80-byte payload.
- `80..83 supported_uplink_records u32`: bit index equals record type value.
- `84..87 supported_downlink_records u32`: bit index equals record type value.
- `88..91 safety_feature_flags u32`
- `92..95 policy_hash u32`
- `96..99 firmware_build_id u32`
- `100..101 host_tx_queue_size u16`
- `102..103 capability_v3_flags u16`
- `104..111 reserved`

Extended 192-byte `CAPABILITY` payload:
- `0..111`: same as the 112-byte payload.
- `112 firmware_identity_version u8`: currently `1`
- `113 git_dirty u8`: `1` means the firmware was built from a dirty local tree
- `114 BOARD_CAN_IRQ_MODE u8`
- `115 reserved u8`
- `116..119 build_epoch_s u32`
- `120..123 firmware_build_id u32`
- `124..127 MCP SPI Hz u32`
- `128..129 CAN record drain budget u16`
- `130..131 configured uplink TX budget KiB u16`; older hosts may display this
  as serial TX ring size.
- `132..143 git short sha ASCII, zero-padded`
- `144..191 PlatformIO env name ASCII, zero-padded`

Extended 224-byte `CAPABILITY` payload:
- `0..191`: same as the 192-byte payload.
- `192 firmware_profile u8`: `1` Passive Product, `2` Full Instrumented.
- `193 profile_lock_state u8`: `1` compile-time locked profile.
- `194 vehicle_impact_state u8`: `1` possible, `2` configured passive,
  `3` verified passive.
- `195 host_command_rx u8`: nonzero means host downlink parser is compiled in.
- `196 control_path u8`: nonzero means host/control CAN TX path exists.
- `197 usb_backpressure_isolated u8`: uplink pressure is isolated from CAN RX
  frontend timing by fixed CAN RX ring plus bounded telemetry queues.
- `198 dtr_reset_sensitive u8`: nonzero means USB CDC disconnect may force MCU
  reset after timeout.
- `199 passive_acceptance_allowed u8`: nonzero only when passive firmware and
  hardware/bench safety case are both verified.
- `200..203 hardware_safety_case_id u32`
- `204..207 bench_verification_id u32`
- `208..211 bus0 passive extension`: mode, ack capability, error-frame
  capability, transceiver reset-safe.
- `212..215 bus1 passive extension`: same layout as bus0.
- `216 usb_cdc_dtr_session_required u8`: nonzero means the Arduino CDC uplink
  requires host DTR assertion before bytes are delivered.
- `217 usb_cdc_dtr_session_only u8`: nonzero means DTR is used only as a USB
  session gate and does not enable board downlink, host CAN TX, control, reset,
  or MCP normal-mode behavior.
- `218..223 reserved`

Extended 272-byte `CAPABILITY` payload:
- `0..223`: same as the 224-byte payload.
- `224 passive_hardware_evidence_schema u8`: currently `1`.
- `225 hardware_silent_strapped_bus0 u8`: claim/reference only.
- `226 hardware_silent_strapped_bus1 u8`
- `227 galvanic_isolated_bus0 u8`
- `228 galvanic_isolated_bus1 u8`
- `229 power_off_passive_bus0 u8`
- `230 power_off_passive_bus1 u8`
- `231 reset_safe_bus0 u8`
- `232 reset_safe_bus1 u8`
- `233 txd_gated_bus0 u8`
- `234 txd_gated_bus1 u8`
- `235 normal_enable_path_populated_bus0 u8`
- `236 normal_enable_path_populated_bus1 u8`
- `237..239 reserved`
- `240..243 field_sku_id u32`
- `244..247 external_analyzer_artifact_id u32`
- `248..251 hotplug_pass_count u32`
- `252..255 host_session_epoch u32`
- `256..259 transport_epoch u32`
- `260..263 usb_attach_quarantine_total u32`
- `264..267 host_absent_gap_total u32`
- `268..271 pre_session_payload_replay_total u32`

Extended 332-byte `CAPABILITY` v7 payload:
- `0..271`: same as the 272-byte payload.
- `272 control_schema u8`: Service/HIL Host control schema, currently `3`.
- `273 terminal_evidence_schema u8`: record 23 schema, currently `2`.
- `274 threshold_qualification u8`: `0 Exploratory`, `1 Frozen`.
- `275 physical_hw_tx_slots u8`: current tracked FDCAN slots, `3`.
- `276..277 host_software_retention u16`: must be `0`.
- `278..279 reserved`.
- `280..283 hw_pending_stale_us u32`
- `284..287 host_proof_timeout_ms u32`
- `288..291 host_proof_ok_total u32`
- `292..295 host_replay_total u32`
- `296..299 host_proof_max_gap_ms u32`
- `300..303 host_proof_mismatch_total u32`
- `304..307 host_proof_timeout_total u32`
- `308..311 permanent_admission_reject_total u32`
- `312..315 transient_admission_reject_total u32`
- `316..319 intentional_cancel_total u32`
- `320..323 terminal_hardware_failure_total u32`
- `324..327 terminal_tracking_failure_total u32`
- `328..331 transmitted_complete_total u32`

State `0` is measurement-only and is not a release approval. Values move to
state `1` only in the order exploratory measurement -> reviewed value decision
-> product constant freeze -> qualification HIL. `120000 B/s` is retired and
`135000 B/s` is not an approved envelope. The enabled steady schema computes
496 records/s and exactly 109556 B/s before qualification headroom is chosen.

The hardware fields above are advertised claims and artifact references. They
do not by themselves prove vehicle-impact-free behavior. VSM may display them
and use them for mismatch detection, but `verified_passive` requires external
analyzer/scope/DTC artifacts whose IDs match these fields.

The product SKU is two-bus ACK-capable observe-only. One-bus passive products are not accepted,
but hosts must keep the one-bus or missing-bus mismatch diagnostic so wrong
firmware uploads are visible.

`usb_attach_quarantine_total` means CDC/uplink/session payload cleanup. It must
not imply stopping CAN front-end drain or resetting/reconfiguring MCP/CAN
transceiver state.

Extended 128-byte `BOARD_HEALTH` payload:
- `0..51`: same prefix as the original 52-byte health payload.
- Prefix byte `46 timer_ok` means the configured encoder-timer requirement is
  satisfied. It is `1` when the encoder lane is disabled; only an enabled lane
  whose timer initialization failed reports `0`.
- `52 health_payload_version u8`: `2` for the old extended payload, `4` for the
  high-load CSM payload
- `53 health_payload_len u8`
- `54..55 reserved u8` (retired GPIO safety model; always zero)
- `56..59 heartbeat_age_ms u32`
- `60..63 lease_remaining_ms u32`
- `64..67 host_crc_fail_total u32`
- `68..71 host_control_state_request_total u32`
- `72..75 host_can_tx_accepted_total u32`
- `76..79 host_can_tx_rejected_total u32`
- `80..87 MCP TX success/fail counters`
- `88..95 J4 built-in CAN TX success/fail counters`
- `96..107 MCP SPI/error/register snapshot`
- `108..111 CAN RX queue depth u32`
- `112..115 host_activation_epoch u32`
- `116..119 backend_flags u32`
- `120..123 host_heartbeat_total u32`
- `124..127 host_control_session_total u32`

Extended 192-byte `BOARD_HEALTH v4` payload:
- `0..127`: same as the 128-byte payload.
- `128..131 bus0_rx_total u32`
- `132..135 bus0_drop_total u32`
- `136..139 bus0_queue_depth u32`
- `140..143 bus0_queue_high_water u32`
- `144..147 bus1_rx_total u32`
- `148..151 bus1_drop_total u32`
- `152..155 bus1_queue_depth u32`
- `156..159 bus1_queue_high_water u32`
- `160..163 serial_enqueue_fail_total u32`
- `164..167 serial_ring_clear_total u32`: compatibility/watchdog field; product
  firmware must keep this at 0 because connected/disconnected TX clear is
  forbidden
- `168..171 serial_ring_cleared_bytes_total u32`: compatibility/watchdog field;
  product firmware must keep this at 0
- `172..175 serial_backpressure_total u32`
- `176..179 uplink_tx_high_water_bytes u32`: maximum observed bytes waiting in
  record queues or in the active typed-frame CDC staging buffer
- `180..183 shared_can_queue_high_water u32`
- `184..187 MCP drain time-budget-hit total u32`
- `188..191 CAN_RX_SEGMENT enqueue-failed frame total u32`

Extended 224-byte `BOARD_HEALTH v5` payload:
- `0..191`: same as the 192-byte v4 payload.
- `192..195 uplink large-pool used blocks u32`
- `196..199 uplink large-pool capacity blocks u32`
- `200..203 uplink large-pool CAN reserve used blocks u32`
- `204..207 CAN truth descriptor queue high-water u32`
- `208..211 payload pool allocation failure total u32`
- `212..215 CAN truth payload pool allocation failure total u32`
- `216..219 descriptor queue combined high-water u32`
- `220..223 diagnostic suppression/drop total u32`

Extended 260-byte `BOARD_HEALTH v6` payload:
- `0..223`: same as the 224-byte v5 payload.
- `224..227 firmware_profile u32`
- `228..231 vehicle_impact_state u32`
- `232..235 can_rx_task_max_us u32`
- `236..239 uplink_pool_high_water_bytes u32`
- `240..243 uplink_descriptor_high_water u32`
- `244..247 usb_reconnect_count u32`
- `248..251 usb_forced_reset_count u32`
- `252..255 passive_violation_latch u32`
- `256..259 capture_invalid_reason u32`

Extended 296-byte `BOARD_HEALTH v7` payload:
- `0..259`: same as the 260-byte v6 payload.
- `260..263 host_absent_rx_discard_bus0_total u32`
- `264..267 host_absent_rx_discard_bus1_total u32`
- `268..271 host_absent_fifo_overflow_total u32`
- `272..275 host_absent_mcp_error_total u32`
- `276..279 host_absent_duration_ms_total u32`
- `280..283 passive_readback_total u32`
- `284..287 passive_readback_violation_total u32`
- `288..291 txreq_violation_total u32`
- `292..295 usb_cdc_dtr_change_total u32`

Extended 360-byte `BOARD_HEALTH v8` payload:
- `0..295`: same as the 296-byte v7 payload.
- `296..303 canonical_publish_seq_next u64`
- `304..311 boot_session_id u64`
- `312..315 usb_connection_epoch u32`
- `316..319 usb_queue_high_water_bytes u32`
- `320..323 usb_offer_overflow_total u32`
- `324..327 usb_frame_sent_total u32`
- `328..331 wifi_connection_epoch u32`
- `332..335 wifi_queue_high_water_bytes u32`
- `336..339 wifi_offer_overflow_total u32`
- `340..343 wifi_frame_sent_total u32`
- `344..347 wifi_connect_total u32`
- `348..351 wifi_disconnect_total u32`
- `352..355 wifi_stall_close_total u32`
- `356..359 publisher_no_sink_drop_total u32`

Extended 384-byte `BOARD_HEALTH v9` payload:
- `0..359`: same as the 360-byte v8 payload.
- `360..363 wifi_socket_error_total u32`: non-`WOULD_BLOCK` socket errors from
  accept, send, receive, or close.
- `364..367 wifi_send_budget_overrun_total u32`: send calls whose measured
  duration exceeded the configured Wi-Fi drain time budget.
- `368..371 wifi_send_call_max_us u32`
- `372..375 wifi_recv_call_max_us u32`
- `376..379 wifi_close_call_max_us u32`
- `380..383 main_loop_max_gap_us u32`: maximum interval observed between main
  loop iterations since boot.

Extended 392-byte `BOARD_HEALTH v10` payload:
- `0..383`: same as the 384-byte v9 payload.
- `384..387 reset_cause_bits u32`: normalized boot reset causes. Bits are
  `0x00000001 power-on`, `0x00000002 brownout`, `0x00000004 pin`,
  `0x00000008 software`, `0x00000010 independent watchdog`,
  `0x00000020 window watchdog`, `0x00000040 CPU`, `0x00000080 domain 1`,
  `0x00000100 domain 2`, `0x00000200 low-power wake`, and
  `0x80000000 unknown`.
- `388..391 reset_status_raw u32`: platform reset-status evidence captured at
  boot. On current Portenta bootloader/framework builds, mbed can report
  `unknown` with raw value `0`; hosts must preserve that result and use a changed
  `boot_session_id` as the authoritative reboot boundary instead of inventing a
  reset cause.

Extended 408-byte `BOARD_HEALTH v11` payload:
- `0..391`: same as the 392-byte v10 payload.
- `392..395 previous_runtime_breadcrumb_valid u32`: `1` only when a
  checksum-valid retained slot from the same firmware build was recovered.
- `396..399 previous_runtime_breadcrumb_stage u32`: low byte is the stage and
  byte 1 is the substep, using the `BOARD_EVENT 41` stage table above.
- `400..403 previous_runtime_breadcrumb_uptime_ms u32`
- `404..407 previous_runtime_breadcrumb_write_sequence u32`
- These fields remain stable for the full new boot so sinks that connect after
  the one-shot event still receive the same reset-boundary evidence.

Extended 472-byte `BOARD_HEALTH v12` payload:
- `0..407`: byte-for-byte the same as the complete 408-byte v11 payload. A v11
  producer remains valid, and a host that only understands v11 must ignore the
  v12 trailing bytes after preserving the original typed payload.
- The typed-frame header `payload_len=472` is authoritative. Legacy prefix byte
  `53 health_payload_len u8` contains only the low 8 bits (`216`) and must not be
  used to truncate an extended payload.
- `408..411 recovery_flags u32`: bit0 recovery ready, bit1 previous retained
  boot valid, bit2 previous boot reached stable state, bit3 current boot reached
  stable state, bit4 Wi-Fi quarantined, bit5 Wi-Fi start allowed, bit6 fallback
  metadata recovered, bit7 firmware source changed, bit8 firmware build changed,
  bit9 a bounded retry is active. Remaining bits are reserved and must be zero.
- `412..415 firmware_source_id32 u32`: deterministic source-tree identity
  projected by the running firmware; this is separate from a Git commit or
  human-readable build label.
- `416..419 boot_sequence u32`
- `420..423 consecutive_early_resets u32`
- `424..427 early_reset_total u32`
- `428..431 wifi_quarantine_total u32`
- `432..435 previous_boot_sequence u32`
- `436..439 previous_last_progress_id u32`
- `440..443 previous_last_progress_detail u32`
- `444..447 previous_last_progress_uptime_ms u32`
- `448..451 current_last_progress_id u32`
- `452..455 current_last_progress_detail u32`
- `456..459 current_last_progress_uptime_ms u32`
- `460..463 retained_event_sequence u32`: sequence of the most recently
  committed retained event visible to this snapshot.
- `464..467 reset_experiment_profile_word u32`: byte0 experiment selector;
  byte1 requested Wi-Fi mode (`0 disabled`, `1 AP-only`, `2 full TCP`); byte2
  effective Wi-Fi mode with the same values; byte3 flags (`bit0 watchdog
  effective`, `bit1 runtime diagnostics enabled`, `bit2 application-data CAN TX
  suppressed`). Bit2 does not imply that the CAN controller cannot emit ACK or
  error signaling. Other byte3 bits are reserved and must be zero in v12.
- `468..471 retained_integrity_word u32`: bits 0..15 valid retained-event count,
  bits 16..23 corrupt metadata count, and bits 24..31 corrupt retained-event
  count. These integrity counters are evidence about retained storage parsing;
  they are not reset-cause classification.

The v12 recovery fields are scalar projections of the retained black box, not a
replacement for its event sequence. A changed `boot_sequence` or
`boot_session_id` proves a reboot boundary; watchdog, brownout, software, and
external-reset causes remain unknown unless separately supported by reset-cause
or retained-progress evidence.

Extended 508-byte `BOARD_HEALTH v13` payload:
- `0..471`: byte-for-byte the same as the complete 472-byte v12 payload.
- v13 extends byte3 of `reset_experiment_profile_word`: bit3 watchdog requested,
  bit4 watchdog start called, bit5 watchdog start returned successfully, and
  bit6 observed timeout matches the requested timeout. Bit7 is reserved.
- `472..475 runtime_contract_id32 u32`: deterministic identity of the selected
  PlatformIO environment and material runtime flags. It distinguishes artifacts
  whose source tree is identical but whose effective runtime contract differs.
- `476..479 recovery_identity_id32 u32`: recovery identity composed from source
  identity, runtime contract identity, and experiment selector.
- `480..483 watchdog_observed_timeout_ms u32`: timeout reported by the live
  watchdog instance; zero when the watchdog is not running.
- `484..487 previous_wifi_call_flags u32`: bit0 valid, bit1 was in progress at
  reset, bit2 completed, bit3 contract changed; byte1 owner and byte2 operation.
- `488..491 previous_wifi_call_boot_sequence u32`
- `492..495 previous_wifi_call_sequence u32`
- `496..499 previous_wifi_call_started_ms u32`
- `500..503 previous_wifi_call_duration_us u32`
- `504..507 previous_wifi_call_result i32`

The Wi-Fi call fields are recovered from a checksum-last dual-slot latch written
immediately before and after vendor calls. An in-progress value localizes the
last entered call boundary; it does not by itself prove the reset cause.

Legacy Mid Carrier MCP2515 profile-major `3` descriptor compatibility:
- Passive Product and Full Instrumented both expose `bus_count=2` for the
  current vehicle product. One-bus passive builds are not product artifacts.
- descriptor 0 describes `bus=0` MCP2515/TJA1050.
- descriptor 1 describes `bus=1` Mid Carrier J4/U2 through the mbed CAN backend.
  Passive Product advertises this lane as ACK-capable observe-only after host
  session stability while keeping host TX/control disabled. Full Instrumented
  may advertise normal/ACK/TX capability for bench/HIL only.
- role was build-profile driven in the retired profiles. Consumers must bind labels and control affordances
  from this descriptor, not from the bus number.
- physical backend `1` MCP2515, transceiver `1` TJA1050.
- `rx_supported=1`, `tx_supported=1`, `control_tx_allowed=1` when
  a Full Instrumented active-capable env is built. Passive Product must report
  `tx_supported=0`, `control_tx_allowed=0`, downlink mask `0`, observe-only bus
  mode, ACK capability `1`, and `passive_acceptance_allowed=0` until hardware
  and bench safety evidence are supplied.
- Classic CAN supported, CAN FD unsupported, max live DLC `8`, nominal bitrate
  `500000`, data bitrate `0`.

Current CSM protocol freeze for VSM:
- VMS must use transport `version=1` and the record IDs/payload sizes in this
  document without alternate live 20-byte modes.
- VMS must parse `CAPABILITY` first and bind bus labels, backend, role, bitrate,
  and control permission from descriptors.
- VSM may send records 24/25 only in an explicitly control-capable profile and
  must use the advertised HNO1 contract/control-island schema.
- VSM must treat `CONTROL_ACK` as board decision evidence only. M4 terminal truth
  is record 26; `CAN_TX_RAW` remains independent physical observer truth and is
  not payload-FIFO correlated to command ID.

RP2040 feeder successor profile major `4`:
- descriptor 0 is `bus=0`, backend `5` RP2040 feeder UART, transceiver `4`
  MCP25625 integrated, RX supported, TX/control unsupported.
- descriptor 1 remains `bus=1` Mid Carrier J4/U2 through the built-in CAN
  backend. Its authority and TX policy are profile driven and unchanged by the
  feeder.
- feeder physical link is Mid Carrier J14 `RX2` (`PG9/USART6_RX`), 1 Mbaud
  8N1, receive-only on M7.
- feeder wire packets use COBS delimiter `0x00`, CRC32C, version `1`, contract
  ID `0x46575231`, boot ID, packet sequence, and per-CAN-frame sequence.
- Sequence ordering uses unsigned 32-bit serial arithmetic within one boot ID.
  Exact wrap from `0xffffffff` to `0` is in order. A forward jump smaller than
  `2^31` is accepted and its missing distance is counted.
- A duplicate/backward packet sequence in the same boot ID rejects the whole
  packet before any CAN frame or status is exposed. A duplicate/backward frame
  sequence rejects only that frame before the CAN callback. The existing
  duplicate/reorder counters count these rejections, while `last_*_sequence`
  remains the last accepted value. A changed boot ID starts fresh packet and
  frame sequence epochs and immediately invalidates the prior status value and
  status timestamp.
- feeder bus readiness requires a live UART ingress, an announced boot session,
  a fresh packet and status in that same session, no source ring/MCP overflow,
  MCP error IRQ, bus-off, or EFLG evidence, and no unrecoverable M7 DMA cursor
  fault. The same predicate drives capability, BOARD_HEALTH, and the status LED.
- M7 feeder service retains the configured byte budget and also applies a
  default 750 us CPU-time budget in chunks no larger than 64 bytes. The normal
  NDTR-before-transfer-complete-callback race may reconcile exactly one wrap
  only when the DMA TC flag is pending. Reconciliation and time-budget-hit
  counters are internal load evidence and are not transport-error events. An
  ambiguous backwards cursor stops the ingress epoch rather than replaying
  bytes.
- accepted feeder frames receive the CSM global capture sequence and are
  published through the existing `CAN_RX_SEGMENT`; no feeder-specific Android
  record type exists.
- BOARD_EVENT codes:
  - `46 FEEDER_LINK_STARTED`: detail `1` initial wire-v1 session, detail `2`
    recovery from stale.
  - `47 FEEDER_SESSION_CHANGED`: detail is wire version; counter is boot ID.
  - `48 FEEDER_SEQUENCE_GAP`: detail `1` packet gap, `2` frame gap, `3` packet
    duplicate/reorder rejection, `4` frame duplicate/reorder rejection.
  - `49 FEEDER_TRANSPORT_ERROR`: detail `1` CRC, `2` parser/contract/length,
    `3` DMA overrun bytes, `4` UART error, `5` DMA transfer error, `6` ingress
    initialization failure, `7` CAN queue callback reject, `8` unrecoverable
    DMA cursor fault.
  - `50 FEEDER_SOURCE_FAULT`: detail `1` source ring overflow, `2` MCP overflow,
    `3` MCP error IRQ, `4` MCP bus-off, `5` MCP EFLG OR.
  - `51 FEEDER_LINK_STALE`: detail `1`; counter is cumulative stale transition.
  - For counter-backed details, the last observed value advances only after the
    BOARD_EVENT is accepted, so temporary evidence-queue pressure retries the
    latest cumulative counter instead of silently consuming it.

## 권장 방향
- 링크 계층: framed transport
- 데이터 계층: typed records
- legacy 20B telemetry는 호환 옵션으로 유지

## framed transport v1
- SOF: `0xA5 0x5A`
- header:
  - `version`: `uint8`, 현재 `1`
  - `record_type`: `uint8`
  - `flags`: `uint8`
  - `seq`: `uint16_le`
  - `payload_len`: `uint16_le`
- payload: record type별 little-endian binary payload
- trailer: `crc16_ccitt_le`
- CRC 범위: `version`부터 payload 마지막 byte까지, SOF 제외
- 수신기는 SOF search + length + CRC로 resync한다.

## 최소 record type
- `1 CAN_RX_RAW`
- `2 CAN_TX_RAW`
- `3 ENC_EDGE_RAW`
- `4 ENC_DERIVED`
- `5 ADC_SAMPLE`
- `6 CONTROL_ACK`
- `7 BOARD_EVENT`
- `8 BOARD_HEALTH`
- `9 CAPABILITY`
- `23 CONTROL_TX_EVIDENCE`
- `25 HOST_CONTROL_NSHOT`
- `26 CONTROL_ISLAND_HEALTH`
- `28 HOST_REALTIME_STATE_V1`
- `29 REALTIME_PROOF_V1`

## 금지
- board direct sensor 값을 가짜 CAN frame으로 위장
- raw를 derived로 덮어쓰기
- overflow/drop을 stats 뒤에 숨기기
