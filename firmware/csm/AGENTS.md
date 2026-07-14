# AGENTS.md - Imported CSM Firmware Router

## Scope
This file applies only to `firmware/csm/**` inside the CSM Remote workspace.

The top-level `C:\WORKS\VS\csm_remote\AGENTS.md` is the product authority for
CSM Remote. This file preserves CSM firmware implementation rules and imported
passive-product constraints.

## Read Order
1. Read `../../AGENTS.md` and `../../README.md` for remote workspace authority.
2. Read `BRIEF.md` for the current imported CSM firmware baseline.
3. For board firmware work, read `board/AGENTS.md` and `board/BRIEF.md`.
4. For remote-control product decisions, read `../../docs/remote/AGENTS.md`.
5. For binary records, transport, replay, or cross-project contracts, read
   `shared/docs/TRANSPORT_AND_RECORDS_KO.md`.

Do not bulk-load every imported CSM document. Load only task-matched docs.

## Authority Boundary
- Remote product identity, M4/M7 split, RC authority policy, and remote evidence
  semantics come from `../../docs/remote/product/PRODUCT_DEFINITION_KO.md`.
- Imported CSM docs under this folder are firmware/passive-product references.
- If imported CSM docs conflict with remote product docs, the remote product docs
  win unless the decision ledger is updated in the same change.

## Routing Rules
- `src/board/remote/**` and `include/board/remote/**`: RC parser, normalizer,
  mailbox contract, and remote source code. These files must not know vehicle
  CAN IDs or perform active IO.
- `src/board/authority/**` and `include/board/authority/**`: M7 authority and
  autonomy arbitration.
- `src/board/control/**` and `include/board/control/**`: operator command,
  command limiting, vehicle mapping, and CAN TX gate. Do not bypass
  `CanTxGateway`.
- `src/main.cpp` and `platformio.ini`: runtime/build-profile wiring. Remote
  runtime enablement must be explicitly guarded by product decisions and tests.
- `shared/docs/TRANSPORT_AND_RECORDS_KO.md`: wire-contract source of truth for
  CSM/VSM typed records.

## Imported CSM Invariants
- The Passive Product baseline is a two-bus ACK-capable observe-only vehicle
  evidence capture artifact.
- Passive Product must compile out host downlink, control, CAN TX, test TX, and
  USB reconnect reset unless a later remote product decision explicitly changes
  that build profile.
- `CONTROL_ACK` is board request evidence. Actual CAN TX success is proven only
  by `CAN_TX_RAW` or a later explicitly defined TX evidence record.
- Hardware evidence fields in `CAPABILITY` are claims and artifact references,
  not physical proof by themselves.
- One-bus passive products are forbidden, but missing-bus or one-bus mismatch
  diagnostics must remain visible to VSM.

## CSM Remote Phase 1 Guard
The current remote skeleton is deny-first and must remain passive until Phase 2
hardware/runtime evidence closes the relevant open decisions.

For Phase 1 skeleton changes, run:

```powershell
python firmware/csm/tools/remote_phase1_guard.py
```

from the workspace root.

For Phase 2A M4 build proof changes, run:

```powershell
python firmware/csm/tools/remote_phase2a_guard.py
```

For Phase 2B M4 Serial3 capture-probe changes, run:

```powershell
python firmware/csm/tools/remote_phase2b_guard.py
```

## Verification Budget
- Docs/harness/comments only: run `git diff --check` and targeted `rg` searches.
- Firmware, `platformio.ini`, passive guard, shared protocol, or capability
  changes: build only the affected PlatformIO env first.
- Product passive build from workspace root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive
```

- M4 remote frontend build proof from workspace root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m4_remote_frontend_build_proof
```

- M4 Serial3 capture probe from workspace root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d firmware/csm -e portenta_h7_m4_remote_serial3_capture_probe
```

- Upload is not build verification. Upload only when explicitly requested and
  when the hardware/vehicle context is safe for MCU reset, USB re-enumeration,
  and CAN-side disturbance.

## Change Rules
- If record wire format changes, update `shared/docs/TRANSPORT_AND_RECORDS_KO.md`
  in the same change.
- If VSM behavior must change, edit the standalone VSM repository separately and
  preserve requested, accepted, actual TX, and feedback as separate states.
- If remote product behavior changes, update the remote product docs under
  `../../docs/remote/**` in the same change.
