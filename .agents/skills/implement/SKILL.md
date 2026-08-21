---
name: implement
description: Use for a bounded CSM bug fix, feature, or refactor that stays inside the active L2 ownership and wire boundaries.
---

# Implement

## Purpose
Complete the smallest product-visible vertical slice inside the active architecture.

## Inputs
- Requested outcome and affected source/test.
- `CURRENT.md`.

## Authority to Read
- `docs/product/PRODUCT_CONSTITUTION_KO.md`.
- `docs/architecture/ACTIVE_ARCHITECTURE.yaml`.
- Only the affected L2 or wire authority.

## Procedure
1. Trace symptom to one owner and failure boundary.
2. Confirm the change fits `IMPLEMENT`; otherwise stop and route to `architecture-change`.
3. Define inputs, outputs, bounded state, failure behavior, and test seam.
4. Implement without parallel legacy path or hidden retry/loss.
5. Search removed names/routes and delete proven residue.

## Stop / Escalation
Escalate on owner, scheduler, protocol architecture, profile identity, or L1 change.
Repeated timing/queue/retry compensation also triggers architecture review.

## Required Verification
Run Constitution, L2 conformance, experiment guards, affected native tests, and exact build where safe.

## Output Contract
Changed/removed files, preserved boundary, executed gates, unexecuted device/HIL, and open risk.
