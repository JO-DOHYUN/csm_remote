---
name: experiment
kind: primary
description: Use for bounded instrumentation, exploratory timing/capacity measurement, or hypothesis testing that must not become production policy.
---

# Experiment

## Purpose
Produce evidence without silently changing product policy.

## Inputs
Git identity, hypothesis, temporary change, measurement and safety scope.

## Authority to Read
Product Constitution, active manifest, affected source, verification policy.

## Procedure
1. Record baseline, hypothesis, temporary delta, measurement, promotion gate, cleanup,
   and `production_authority: false`.
2. Keep safety/authority fail closed and bound all resources.
3. Do not tune product criteria to pass the experiment.
4. Run the minimum controlled measurement and preserve raw evidence/hash.
5. Remove temporary code or keep it isolated with an explicit open cleanup gate.

## Stop / Escalation
Stop before vehicle/hardware write without explicit authorization. Promotion requires reviewed
measurement, product-constant freeze and qualification; result alone is not policy.

## Required Verification
Experiment guard, affected deterministic tests, and measurement integrity checks.

## Output Contract
Hypothesis, exact baseline/delta, result, uncertainty, promotion decision, cleanup state.
