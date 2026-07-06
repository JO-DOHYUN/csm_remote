# AGENTS.md - Repository Router

## Purpose
This root file is a routing guide for agents working in the standalone CSM
firmware repository. The product identity is a two-bus ACK-capable observe-only CAN
front-end with typed evidence uplink. VSM/Qt and Android app work must live in
their own repositories, not inside this project folder.

## Read Order
1. Read `BRIEF.md` for the current repository-level state.
2. For CSM firmware work, read `board/AGENTS.md` and `board/BRIEF.md`.
3. For binary records, transport, replay, or cross-project contracts, read
   `shared/docs/TRANSPORT_AND_RECORDS_KO.md`.
4. For final product completion boundaries, read
   `board/docs/CSM_FINAL_PRODUCT_COMPLETION_TARGET_KO.md`.
5. For current integration architecture, read
   `VMS_CSM_03_ARCHITECT_SYNTHESIS_FINAL.md` as a decision input, then apply it
   through the scoped docs instead of treating it as a replacement prompt.

## Routing Rules
- Do not bulk-load every document. Load only the task-matched docs listed in the
  scoped AGENTS files.
- Hardware wiring/setup/testing/verification work must use
  `board/docs/HARDWARE_BRINGUP_GATE_HARNESS_KO.md` first, then may use
  `board/docs/HARDWARE_TEST_AI_AUX_KO.md`. General code or document work should
  not load those hardware-only auxiliary files.
- The live production stream is typed transport v1 only. Legacy 20-byte data is
  replay/import compatibility, not a live output mode.
- `CONTROL_ACK` is board request evidence. Actual CAN TX success is proven only
  by `CAN_TX_RAW`.
- CSM has explicit artifacts:
  - Passive Product: two-bus ACK-capable observe-only vehicle evidence capture.
  - Full Instrumented: bench/HIL control work.
  - Bench ACK/TX test: lab-only counterpart for Kvaser/PCAN transmit tests.
  Do not use Full Instrumented or Bench ACK/TX as passive acceptance evidence.
- Passive Product is always a two-bus observe-only artifact for the current
  vehicle use case. It must compile out host downlink, control, CAN TX, test TX,
  and USB reconnect reset. Controlled MCP/built-in CAN mode transitions are
  allowed only to move between deferred CAN front-end hold and session-stable
  ACK-observe. It is not `verified_passive` until hardware safety case and bench
  verification IDs are nonzero.
- Hardware evidence fields in `CAPABILITY` are claims and artifact references.
  They are not proof by themselves; vehicle-impact-free PASS requires external
  analyzer/scope/DTC evidence.
- CDC session open/close/DTR/re-enumeration must not enable host
  downlink/TX/control. In the Passive Product field build, CAN front-end
  initialization itself is deferred until CDC/DTR session stability plus the
  configured quiet window; host absent service must not stage typed frame
  payloads.
- USB attach quarantine is CDC/uplink/session payload cleanup only. After
  quarantine and quiet-window deferred CAN initialization, the firmware may
  enter ACK-capable observe mode. Session close must return to no-ACK
  pre-session hold.
- Passive Product is ACK-capable observe-only after a stable host session.
  ACK capability is not host TX/control capability. Pre-session no-ACK is a safe
  lifecycle state, not a product transmit failure.
- One-bus passive products are forbidden, but missing-bus or one-bus capability
  mismatch diagnostics must remain visible to VSM.
- `shared/docs/TRANSPORT_AND_RECORDS_KO.md` is the CSM/VMS wire-contract source
  of truth.
- Do not create or maintain nested `qt/`, `vsm/`, `android/`, or duplicate
  `csm/` project workspaces under this CSM repository. Build and upload CSM only
  from this repository root with PlatformIO.
- Bus labels are profile/capability-driven. The Passive Product default env is
  `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`, which exposes MCP2515/TJA1050
  as `bus=0` and Mid Carrier J4/U2 as `bus=1`; both are observe-only,
  ACK-capable after session stability, and host TX/control disabled.
  The Full Instrumented env keeps both buses available for bench/HIL RX and
  audited control TX. One-bus passive builds are not product artifacts.
- The only field/product upload target is
  `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`. Do not build or upload
  one-bus, full, or lab envs while validating vehicle passive behavior unless
  the user explicitly switches to bench diagnostics.

## Verification Budget
- Do not build by habit. Select the smallest proof that covers the changed
  surface.
- Docs/harness/comments only: run `git diff --check` and targeted text search.
  Do not run PlatformIO.
- Firmware, `platformio.ini`, passive guard, shared protocol, or capability
  changes: build only the affected env first.
- Build the passive alias, full instrumented env, or lab ACK/TX env only when
  profile separation, wire compatibility, or that specific artifact changed.
- Upload is not build verification. Upload only when explicitly requested and
  the current hardware/vehicle context is safe for MCU reset, USB
  re-enumeration, and CAN-side disturbance.
- VSM/CSM system work must inspect both repositories, but edit, build, upload,
  commit, and push each repository from its own root.

## Change Rules
- If record wire format changes, update `shared/docs/TRANSPORT_AND_RECORDS_KO.md`
  in the same change.
- If MCP behavior changes, preserve MCP `bus=0`, keep bus role/backend exposed
  through `CAPABILITY`, and do not restore the failed MCP falling-edge INT gate.
- If VSM behavior must change, do that in the standalone VSM repository and keep
  requested, host write, accepted, sent audit, and feedback as separate states.
- If passive lifecycle evidence changes, update `include/protocol/TypedRecords.h`
  and `shared/docs/TRANSPORT_AND_RECORDS_KO.md` in the same change.
