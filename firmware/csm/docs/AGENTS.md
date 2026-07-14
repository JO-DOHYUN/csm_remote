# AGENTS.md - Imported CSM Docs Router

## Scope
This file applies to `firmware/csm/docs/**`.

These documents are imported CSM firmware references. They are not the top-level
CSM Remote product definition.

## Required Context
Before using these docs for CSM Remote work, read:

1. `../../../AGENTS.md`
2. `../../../docs/remote/AGENTS.md`
3. `../AGENTS.md`

## Rules
- Do not follow old Qt-folder router references as active paths in this
  workspace.
- Treat old synthesis/prompt documents as historical decision inputs.
- If a CSM doc conflicts with `../../../docs/remote/product/PRODUCT_DEFINITION_KO.md`,
  the remote product definition wins.
- If a wire-record contract changes, update `../shared/docs/TRANSPORT_AND_RECORDS_KO.md`
  and the remote requirement trace in the same change.
