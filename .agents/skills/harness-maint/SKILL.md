---
name: harness-maint
kind: primary
description: Use for AGENTS, Git-first authority routing, skills, guards, exec-plan lifecycle, and stale harness/build-route cleanup.
---

# Harness Maintenance

## Purpose
Keep one deterministic minimal-context route without changing runtime semantics.

## Inputs
Requested harness outcome, Git HEAD/status/diff and dirty-worktree inventory.

## Authority to Read
Approved exec-plan when present, Product Constitution, active manifest, `docs/index.md`,
and only affected harness files.

## Procedure
1. PREFLIGHT Git state and preserve unrelated dirty state.
2. Classify facts as L0/L1/L2/L3/HISTORY.
3. Establish canonical owner before deleting old routes.
4. Use references, source, tests, builds and history to prove residue obsolete.
5. Update routes/guards/indexes; keep HISTORY outside default context.
6. Re-search duplicate authority, broken/stale routes and generated artifacts.

## Stop / Escalation
Do not alter runtime or infer current facts. Evidence-insufficient cleanup remains `UNRESOLVED`.

## Required Verification
`python tools/verify_harness.py`, L1/L2/L3 guards, route scenarios, link/stale search and diff audit.

## Output Contract
Authority migration, deleted duplicates, preserved runtime, checks and unresolved items.
