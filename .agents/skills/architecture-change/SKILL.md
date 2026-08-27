---
name: architecture-change
kind: primary
description: Use when proposing or changing CSM ownership, dataflow, scheduler, queue topology, protocol execution, profile boundary, or core placement.
---

# Architecture Change

## Purpose
Review or execute an L2 boundary change while protecting L0/L1.

## Inputs
- Git HEAD/status/diff and the owner task or matching active exec-plan.
- Proposal/evidence/constraints for `decision_state: review`.

## Authority to Read
- Product Constitution.
- Active architecture manifest and affected L2 documents.
- Source/test/build routes that prove the current boundary.
- History only by targeted decision search.

## Procedure
1. Resolve owner -> input -> state -> output -> consumer -> failure -> proof.
2. For `review`, compare current, proposal, a simpler/better third option, and no-change.
3. For `approved`, skip comparison and execute every required migration/cleanup/proof item.
4. Judge authority clarity, safety, determinism, stale behavior, evidence, boundedness,
   failure containment, hardware feasibility, migration/proof cost, and maintainability.
5. Check whether a local delay, queue, retry, or timing workaround hides an ownership defect.
6. Change manifest, prose, L2 guard, source and tests atomically; complete the exec-plan lifecycle.

## Stop / Escalation
Current guard failure alone is not a rejection reason. L1 impact requires explicit `PRODUCT_CHANGE` approval.

## Required Verification
Harness + Constitution + adopted L2 guard + affected builds/tests. Hardware claims require actual HIL.

## Output Contract
Comparison verdict, selected boundary, rejected alternatives, migration/cleanup, proof and unresolved risk.
