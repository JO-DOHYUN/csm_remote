# CSM–VSM Network Transport Architecture

Authority: `L2 ACTIVE ARCHITECTURE / BINDING IN IMPLEMENT`

## Frozen ownership

- Android `AndroidVsmNetworkManager` owns one app-scoped CSM Wi-Fi `Network`,
  route and high-performance lock. `onLost` advances network truth and exposes
  epoch/time/count as diagnosis only.
- TCP `3333` is canonical telemetry/evidence. Its queue, epoch and failure do
  not own Host motion permission.
- TCP `3334` is reliable low-rate ARM, DISARM, N-shot and query transaction
  transport. It does not carry periodic state or liveness renewal.
- UDP `3335` carries one exact `HOST_REALTIME_STATE_V1` (64-byte payload) every
  20 ms and returns exact cumulative `REALTIME_PROOF_V1` (36-byte payload).
  Each endpoint retains only its newest state/proof; no retry, replay, queue or
  catch-up is permitted.
- M7 owns receiver-local bidirectional Host liveness. M4 TIM4/FDCAN1 execution,
  safe-wire values, cadence and terminal truth are unchanged.

## State and failure lifecycle

```text
Android semantic image -> depth-1 UDP state -> M7 validate/authority
  -> depth-1 global source -> M4 TIM4/FDCAN1
M7 cumulative proof <- latest-only UDP proof <- applied state/authority truth
```

The Android realtime sequence is process-scoped and does not rewind across
ARM→DISARM→ARM. CSM accepts a new inactive sender sequence domain only from a
valid PRE-ARM packet. Boot, DISARM, liveness expiry and an inactive TCP epoch
discard prior PRE-ARM proof and require a proof issued after that boundary.
Delayed ACTIVE packets cannot establish or poison the new inactive domain.
Within one transport epoch ACTIVE requires a wrap-safe strictly newer Host
authority epoch for transaction/proof correlation. Android identity never
enters M4's persistent activation watermark. M7 allocates one global monotonic
physical activation epoch across Host and RC, synchronized forward from the
last M4-observed epoch. An inactive new TCP epoch may establish a replacement
Host authority namespace only after the new PRE-ARM challenge is returned. Each
accepted Host ARM is bound to the current M4 boot identity; a later M4 boot or
a locally observed M4 ACTIVE revoke retires the M7 Host authority. Recovery
therefore requires fresh PRE-ARM plus explicit ARM and never resumes old motion.
The snapshot carries that admitted `target_m4_boot_id`; M4 rejects retained or
republished ACTIVE for an older boot before any physical release. This closes
the interval before M7 receives reboot health. A local not-ready condition also
retires Host authority before the first matching M4 ACTIVE observation.
The realtime `STATE_APPLIED` proof flag is asserted only after both the state
was accepted by M7 and matching M4 boot/physical activation was observed.

Operator stop first stages the neutral UDP image independently of TCP `3333`
observer availability or either TCP epoch. Fresh `3333` evidence may confirm
the physical neutral terminal and TCP `3334` may then carry DISARM, but neither
may delay the realtime neutral transition. A receiver-local expiry proof keeps
the retired authority epoch until Android consumes the terminal status; the
next valid PRE-ARM packet starts authority zero again.

Loss/reorder/invalid state or proof cannot refresh liveness. At the initial
350 ms receiver-local boundary, M7 revokes Host ACTIVE and M4 falls back to its
frozen safe-wire policy. TCP `3333` loss is observation loss only. TCP `3334`
loss prevents transactions but does not itself revoke a healthy UDP authority.
Wi-Fi `onLost` is shared transport evidence, never a second safety owner.

## Capacity and evidence

Pinned lwIP owns six socket-arena slots: five application sockets (two TCP
listeners, up to two accepted TCP clients, one UDP socket) plus one measured AP
internal owner. The generated `libmbed.a`, `mbed_config.h` and artifact manifest
are one versioned build artifact.

Service/HIL exposes bounded first/current evidence for network epoch/loss,
UDP state sequence/generation/send result, CSM RX/admission/applied generation,
proof sequence/ref/status/reason/send result, TCP transaction write/ACK and
TCP `3333` telemetry progress. Evidence never gates runtime or adds backlog.
Proof destination is copied from the exact decoded request through the depth-1
mailbox. Later raw UDP traffic cannot redirect an admitted proof. The worker
rejects proofs whose RX token predates the current socket open, including proofs
staged late by M7 after a socket close.

## Physical qualification

Software tests prove encoding, lifecycle, bounded storage and owner separation.
Fault injection must separately test TCP `3334` RTO, TCP `3333` stall, forward
UDP drop, proof-return UDP drop, total Wi-Fi loss and repeated re-ARM. Timeout
or capacity changes require measured evidence; missing HIL remains `NOT RUN`.
