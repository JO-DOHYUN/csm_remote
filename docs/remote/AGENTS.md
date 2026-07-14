# AGENTS.md - CSM Remote Document Router

## Scope
This file controls the CSM Remote product-definition and development-harness
documents under `docs/remote/**`.

It is the Codex-facing router for product decisions. It replaces the old flat
root remote-document entry style.

## Read Order
1. `README.md`
2. `docs/remote/harness/ROUTING_MATRIX_KO.md`
3. Task-matched documents from the map below
4. `docs/remote/product/CHANGE_HISTORY_KO.md` only when history or handoff
   context is needed

Do not read the full product definition by habit. Read the controlling sections
needed for the task.

## Authority Order
1. `docs/remote/product/PRODUCT_DEFINITION_KO.md`
2. `docs/remote/product/OPEN_DECISIONS_KO.md`
3. `docs/remote/product/DECISION_LEDGER_KO.md`
4. `docs/remote/product/REQUIREMENT_TRACE_KO.md`
5. `docs/remote/architecture/**`
6. `docs/remote/reviews/**`
7. Imported CSM firmware docs under `firmware/csm/**`

If two documents conflict, the higher item wins unless the decision ledger
explicitly supersedes it.

## Task Routing
| Task | Read | Do Not Read By Default |
|---|---|---|
| Direction check or short review | `README.md`, this file, relevant routing row | Full source tree |
| Product purpose or identity change | `product/PRODUCT_DEFINITION_KO.md`, `product/OPEN_DECISIONS_KO.md`, `product/DECISION_LEDGER_KO.md` | Firmware implementation files first |
| Harness or Codex workflow change | `harness/ROUTING_MATRIX_KO.md`, `harness/DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md` | Vehicle CAN docs |
| M4 RC parser/frontend | `architecture/CRSF_FRONTEND_CONTRACT_KO.md`, `architecture/M4_M7_REMOTE_MAILBOX_CONTRACT_KO.md`, `operations/PHASE2B_R16SM_SERIAL3_CAPTURE_RUNBOOK_KO.md` when hardware capture is involved, relevant product sections | VSM UI details |
| M4-M7 IPC/mailbox | `architecture/M4_M7_REMOTE_MAILBOX_CONTRACT_KO.md`, OD-004 in `product/OPEN_DECISIONS_KO.md` | Vehicle CAN mapping details |
| M7 authority | product authority/autonomy sections, `product/OPEN_DECISIONS_KO.md`, related firmware authority files | CRSF protocol details |
| Vehicle mapping or CAN TX gate | product CAN TX/evidence sections, `architecture/SOURCE_LAYOUT_PROPOSAL_KO.md`, control source files | VSM UI details |
| VSM or Android expansion | product VSM/evidence sections, `product/OPEN_DECISIONS_KO.md` | M4 parser internals unless wire records change |
| Phase closure or audit | `reviews/PHASE1_SKELETON_AUDIT_KO.md`, `product/REQUIREMENT_TRACE_KO.md`, `product/CHANGE_HISTORY_KO.md` | Old handoff as authority |

## Change Rules
- Product definition changes must update decision/open-decision state in the same
  change.
- Code changes that satisfy or alter a requirement must update
  `product/REQUIREMENT_TRACE_KO.md`.
- Phase-level implementation changes must update `product/CHANGE_HISTORY_KO.md`.
- Original handoff text in `reviews/AUTHORITY_FINAL_HANDOFF_ORIGINAL.md` is
  reference only.

## Verification
For doc-only remote changes, run:

```powershell
git diff --check
Get-ChildItem -File | Where-Object { $_.Name.StartsWith('CSM' + '_REMOTE_') }
rg -n (('standalone ' + 'CSM') + '|' + ('TODO' + '-path')) README.md AGENTS.md docs/remote firmware/csm/AGENTS.md firmware/csm/BRIEF.md firmware/csm/board/AGENTS.md -g "*.md"
```
