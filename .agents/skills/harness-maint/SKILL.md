---
name: harness-maint
description: Use only for AGENTS, CURRENT, authority routing, skills, guards, decision index, and stale harness/build-route cleanup.
---

# Harness Maintenance

## Purpose
Keep one deterministic minimal-context route without changing runtime semantics.

## Inputs
Requested harness outcome, both repository baselines, dirty-worktree inventory.

## Authority to Read
CURRENT, Product Constitution, active manifest, `INDEX.md`, and only affected harness files.

## Procedure
1. PREFLIGHT baselines and preserve unrelated dirty state.
2. Classify facts as L0/L1/L2/L3/HISTORY.
3. Establish canonical owner before deleting old routes.
4. Use references, source, tests, builds and history to prove residue obsolete.
5. Update routes/guards/indexes; keep HISTORY outside default context.
6. Re-search duplicate authority, broken/stale routes and generated artifacts.

## Stop / Escalation
Do not alter runtime or infer current facts. Evidence-insufficient cleanup remains `UNRESOLVED`.

## Required Verification
`python tools/verify_harness.py`, all three control guards, link/stale search, diff audit.

## Output Contract
Authority migration, deleted duplicates, preserved runtime, checks and unresolved items.
