---
name: architecture-change
description: Use when proposing or changing CSM ownership, dataflow, scheduler, queue topology, protocol execution, profile boundary, or core placement.
---

# Architecture Change

## Purpose
Review L2 as a replaceable baseline while protecting L0/L1.

## Inputs
- Operator-visible proposal, evidence, constraints, and rollback.
- `CURRENT.md`.

## Authority to Read
- Product Constitution.
- Active architecture manifest and affected L2 documents.
- Source/test/build routes that prove the current boundary.
- History only by targeted decision search.

## Procedure
1. Separate verified fact, decision, assumption, and open decision.
2. Compare current, proposal, a simpler/better third option, and no-change.
3. Judge authority clarity, safety, determinism, stale behavior, evidence, boundedness,
   failure containment, hardware feasibility, migration/proof cost, and maintainability.
4. Check whether a local delay, queue, retry, or timing workaround hides an ownership defect.
5. Choose one owner/route; define migration, cleanup, rollback, and proof.
6. Change manifest, prose, conformance guard, source, tests, and active index atomically after approval.

## Stop / Escalation
Current guard failure alone is not a rejection reason. L1 impact requires explicit `PRODUCT_CHANGE` approval.

## Required Verification
Harness + Constitution + adopted L2 guard + affected builds/tests. Hardware claims require actual HIL.

## Output Contract
Comparison verdict, selected boundary, rejected alternatives, migration/cleanup, proof and unresolved risk.
