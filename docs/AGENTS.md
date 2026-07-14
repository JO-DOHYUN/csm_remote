# AGENTS.md - Documentation Router

## Scope
This file routes documentation work under `docs/**`.

Always apply the workspace root `AGENTS.md` first. This file only narrows the
document path.

## Routing
- For `docs/remote/**`, read `docs/remote/AGENTS.md`.
- Do not treat documentation edits as firmware acceptance evidence.
- When moving or renaming docs, update `README.md`, `docs/remote/AGENTS.md`, and
  references found by `rg`.

## Verification
For documentation-only changes, run:

```powershell
git diff --check
Get-ChildItem -File | Where-Object { $_.Name.StartsWith('CSM' + '_REMOTE_') }
rg -n "standalone CSM|TODO-path" README.md AGENTS.md docs/remote firmware -g "*.md"
```
