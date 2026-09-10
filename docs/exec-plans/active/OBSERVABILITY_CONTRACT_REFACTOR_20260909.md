---
authority: PROCEDURE
owner: cross-repository-observability-route
status: active
read_when: the matching observability migration is requested from the CSM repository
---

# Observability Contract Refactor — CSM Route Pointer

This file owns no runtime policy, observation schema, field layout, phase result, or
product verdict. The canonical approved execution plan is in the Android repository:

```text
repository: JO-DOHYUN/vsm_android_app
plan: docs/exec-plans/active/OBSERVABILITY_CONTRACT_REFACTOR_20260909.yaml
```

Resolve the Android root explicitly for cross-repository work, or use unique workspace
sibling discovery. Read the canonical plan, its selected RUNBOOK phase, then the
matching CSM authority/source/test. Do not copy plan content here.

For P0, use `harness-maint`; run the local H0 and route scenarios independently.
This pointer does not make unimplemented observation coverage a PASS and does not
authorize source, device, HIL, commit, or push work.
