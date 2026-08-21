---
name: verification
description: Use for CSM deterministic tests, exact builds, evidence audit, release gates, soak, or cross-system verification.
---

# Verification

## Purpose
Match each claim to the gate that can actually prove it.

## Inputs
Candidate baseline/artifact identity, requested claim, affected boundary.

## Authority to Read
CURRENT, verification policy, active manifest, affected contract/test tool.

## Procedure
1. Classify claim as static, native, build, device, bench/HIL, vehicle, or soak.
2. Verify exact source/env/artifact identity before using evidence.
3. Run safe deterministic gates first; do not alter criteria after seeing results.
4. For failure: symptom -> evidence -> root-cause boundary -> hypothesis -> minimal experiment.
5. Report measured output and distinguish PASS, FAIL, BLOCKED, and NOT RUN.

## Stop / Escalation
No upload, CAN transmit, vehicle motion, or remote write without explicit authorization and target check.

## Required Verification
The affected level in `docs/quality/VERIFICATION_POLICY_KO.md`; build never substitutes for HIL.

## Output Contract
Exact commands, identity, results, artifacts, unexecuted gates, field risk, verdict.
