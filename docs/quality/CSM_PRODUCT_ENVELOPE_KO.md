# CSM Product Envelope

Updated: 2026-07-27

This document is the release-facing envelope for the Portenta H7 + Feather
RP2040 CAN-feeder product. The executable calculation is
`firmware/csm/tools/product_envelope.py`; values copied into prose are not
authority.

## Implemented candidate data plane

```text
M4 CRSF latest sample ----\
Feeder UART DMA CAN0 ------> M7 bounded source queues
J4 FDCAN CAN1 ------------/       |
                                   v
                         RecordAdmission
                                   |
                         CanonicalPublisher
                      (one identity, encode once)
                         /                   \
             UsbCdcSink queue        WifiTcpSink mailbox
                                           |
                                  one Wi-Fi RTOS worker
                         WHD AP service + Mbed TCPSocket
```

- RC, CAN ingest, authority, safety, and the canonical publisher never call or
  wait for a network API.
- USB and Wi-Fi own independent bounded queues and epochs. One blocked sink
  cannot block the other sink or a source.
- Wi-Fi uses `WhdSoftAPInterface` directly and exposes only the product AP.
  Portenta WHD is started with `ap_sta_concur=true`: the same compatibility
  role used by the measured Arduino path. A clean A/B run with `false` accepted
  only 536 bytes from a Windows consumer before permanent `WOULD_BLOCK` and a
  5-second stall close, so the unvalidated AP-only role is not a product
  optimization. The Arduino `WiFiServer` wrapper remains outside the path.
- The worker alone owns AP, listen socket, accepted socket, send, receive,
  abort, and close. Accepted Mbed sockets remain close-only.
- Admission priority and delivery latency are separate. `CAN_TX_RAW` remains
  critical retention evidence but is batchable. Session, control ACK, and board
  events use a 10 ms latency bound. Other records batch to 1,024 bytes or 75 ms.
- Positive socket return bytes are the only bytes consumed from the queue.
  Partial writes preserve the remaining bytes and original record boundaries.

## Queue and failure contract

- Wi-Fi queue: 1,280 descriptors and 65,520 payload bytes.
- Critical reserve: 4 descriptors and 2,112 bytes.
- Normal byte envelope: 63,408 bytes. This covers 100 kbit/s for 5 seconds
  (`62,500` bytes) with 908 bytes of margin.
- A 96% byte/descriptor high-water requests exactly one `QueuePressure` epoch
  close before Reserved/Full. A separate 5 second zero-progress timer closes
  `TransmitNoProgress`.
- Reconnect always starts a new epoch and session anchor. Disconnected bytes
  are not silently replayed. Durable lossless replay would require a separate
  flash + ACK protocol and is not part of this product.

## Wire envelope

`CAN_RX_SEGMENT` schema 2 is lossless:

- header 40 bytes; entry 20 bytes; maximum 23 frames;
- full typed frame 511 bytes, within one 512-byte USB packet;
- capture sequence and monotonic timestamp are reconstructed from base values
  plus checked deltas;
- the two CAN source queues are merged by the smallest `capture_seq`, not
  round-robin;
- Android, Windows, and PC tools retain legacy 32/30 decoding and reject an
  unknown schema visibly.

At 2,000 full-DLC CAN frames/s, schema 2 reduces CAN receive transport from
65,762 to 44,437 B/s. With the current 250 Hz control evidence and health
records, the calculated product stream is 57,735 B/s. The prior STA+AP raw
baseline was 65,083--69,413 B/s, leaving only 11.29--16.82% capacity margin.
Therefore 2,000 fps requires a physical soak; calculation alone is not release
evidence.

At 4,000 aggregate fps the calculated stream is about 102,172 B/s. The measured
raw baseline cannot carry that. Therefore USB remains the full-rate truth path
at this load until a higher-capacity external transport passes the hardware
gate.

## Physical gate result

The final nonblocking candidate was built and uploaded with source manifest
`341f948e99256a0cb07fd6883f64575c9a76b4038c11bb238ba1350fb14eec2e`
and firmware SHA-256
`20A84878DEEF7541726F95494550B7F8F3B90E15041474E8A1C816E974507281`.
On a fresh boot, the 15.059 s idle product gate failed after the PC received
2,954 bytes. The socket then produced 469 `WOULD_BLOCK` results and the worker
closed the epoch at the independent 5 second no-progress boundary.

The failure did not propagate into the deterministic core: the same window
received 945 CAN frames with zero CAN/FIFO/USB overflow, zero CRC or
typed/segment/capture gap, one boot session, and a 3,311 us maximum main-loop
gap. This proves worker isolation, not Wi-Fi release readiness. The 2,000 fps
Wi-Fi gate was intentionally skipped because the idle prerequisite failed.
Retained earlier 2,000 fps evidence still proves feeder, CSM ingest, and USB
truth stay lossless while Wi-Fi isolates on queue pressure.

Therefore the internal Portenta WHD/lwIP path is a release blocker. Queue or
RAM enlargement is not an approved remedy: failure occurs below queue capacity
and after the lower socket stops progressing. The next admissible gate is
either a reproducible pinned Mbed/lwIP build with measured D2 ownership and an
idle/nominal/2,000 fps pass, or a higher-capacity external transport. Synthetic
raw throughput is not product evidence.

The candidate also remains blocked on four code-review items before another
release build: atomic queue-pressure/state acknowledgement, rollback after
partial AP startup, one coherent diagnostic revision, and an enforceable
latency-bound-record queue-delay policy.

## Memory ownership

- Wi-Fi DTCM raw storage: 20,480 descriptor bytes + 65,520 payload bytes =
  86,000 bytes.
- Usable M7 DTCM: 130,408 bytes. The 32-byte queue alignment consumes up to
  8 bytes at the current origin, so at most 44,400 bytes remain; the linker
  independently enforces a minimum 32 KiB safety reserve after all DTCM input.
- Queue cursors, atomics, socket objects, and DMA-visible buffers remain in
  normally initialized D1/D2 memory. Only raw CPU-owned queue storage is
  `NOLOAD` DTCM.
- Per-bus CAN ingest queues are 512 entries. At 2,000 fps per bus they cover
  256 ms and save 229,376 bytes versus the old 4,096-entry pair.
- M4 owns physical D2 through `0x30040000`; M7 lwIP/DMA owns
  `0x30040000..0x30048000`. Paired linkers assert the boundary.

## Objective release table

Generate the theoretical table:

```powershell
python firmware/csm/tools/product_envelope.py
python firmware/csm/tools/product_envelope.py --format json --output artifact.json
```

The physical table must add, for each load point: input fps/bytes, canonical
accepted B/s, socket B/s, request mean, positive-write mean, queue high-water,
Reserved/Full, disconnect reason, sequence/CRC/capture gaps, source drops,
main-loop maximum gap, and worker stack floor. A row passes only when every
required integrity counter is zero and measured throughput/queue conservation
matches the calculated envelope.
