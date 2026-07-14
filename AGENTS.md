# AGENTS.md - CSM Remote Workspace Router

## Scope
This is the top-level agent instruction file for `C:\WORKS\VS\csm_remote`.
It is the first routing authority for the CSM Remote product workspace.

Nested `AGENTS.md` files add local rules only. They must not override the remote
product definition, M4/M7 responsibility split, authority policy, or evidence
contract defined from this workspace root.

## Mandatory Entry Path
For every task in this workspace:

1. Read `README.md` for the current baseline and document priority.
2. Read `docs/remote/AGENTS.md` for task-specific document routing.
3. Load only the task-matched documents listed there.
4. If the task enters `firmware/csm/**`, also read `firmware/csm/AGENTS.md`.

Do not bulk-load every Markdown file. The automatic path is this file first,
then the scoped router for the touched path.

## Product Authority
The controlling product definition is:

```text
docs/remote/product/PRODUCT_DEFINITION_KO.md
```

The English definition is a companion only. Original handoff and imported CSM
documents are background unless the current scoped router explicitly promotes
them for a task.

## Non-Negotiable Invariants
- No confirmed autonomy release means zero local vehicle CAN TX.
- M4 is the RC receiver/parser frontend only.
- M7 owns authority, safety, command limiting, vehicle mapping, and CAN TX gate.
- VSM is observer-only by default.
- `CONTROL_ACK` or an accepted command is not proof of physical CAN TX.
- Actual CAN TX success requires explicit TX evidence such as `CAN_TX_RAW`.
- Product/default builds must remain deny-first until hardware and evidence
  decisions are explicitly closed.

## Path Routing
- `docs/remote/**`: use `docs/remote/AGENTS.md`.
- `firmware/csm/**`: use `firmware/csm/AGENTS.md`; remote product authority still
  comes from this root and `docs/remote/**`.
- `firmware/csm/src/board/remote/**`: RC frontend/parser/mailbox code. Keep it
  independent from CAN IDs and vehicle mapping.
- `firmware/csm/src/board/authority/**`: M7 authority and autonomy arbitration.
- `firmware/csm/src/board/control/**`: operator command, limiter, mapper, and CAN
  TX gate. Do not bypass `CanTxGateway`.

## CSM Imported Documentation
The imported CSM firmware carries older CSM/VSM/passive-product documents. Keep
them as scoped firmware references. They are not allowed to redefine the CSM
Remote product unless `docs/remote/product/PRODUCT_DEFINITION_KO.md` is updated
in the same change.

Do not delete imported CSM documents just because they are not always read. They
preserve baseline, hardware, passive product, and wire-contract evidence.

## Verification Policy
- Docs/harness-only changes: run `git diff --check` and targeted `rg` searches.
- Remote Phase 1 skeleton guard: run `python firmware/csm/tools/remote_phase1_guard.py`.
- Firmware changes: build the affected PlatformIO env from `firmware/csm`.
- Upload is never implicit verification. Upload only when explicitly requested
  and when the hardware context is safe.
