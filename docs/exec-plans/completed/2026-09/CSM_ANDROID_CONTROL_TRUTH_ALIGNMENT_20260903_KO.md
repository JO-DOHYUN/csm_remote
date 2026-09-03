---
authority: L0_EXECUTION
decision_state: approved
owner: product-owner-task-2026-09-03
status: complete
completed_at: 2026-09-03
---

# CSM/Android control truth alignment

## Frozen boundary

REV.B ownership, 005/007/364 cadence and SAFE bytes, RC priority, Host causal
transport, and vehicle wire semantics do not change.

## Completed closure

1. Host N-shot transaction watermark persists across state replacement,
   completion/cancel, DISARM and re-ARM, and resets only for M7 boot or a new
   Host control transport epoch.
2. M4 CRSF foreground drain is byte/time bounded and publishes budget-hit,
   parser and receiver-admission truth without delaying control IPC/health.
3. `REMOTE_CONTROL_STATE` schema 4 carries that truth through the canonical CSM
   record into generated Android constants, reducer and diagnostics.
4. Steering endurance derives physical terminal-progress failure only from M4
   TIM4/counter progress; Android observation age remains evidence freshness.
5. Obsolete active Host CAN request tools were removed. Cross-repository source
   discovery now requires an explicit root or exactly one valid sibling.

## Completion evidence

- CSM H0/L1/L2/L3/wire guards: PASS
- all CSM native contract runners: PASS
- M7 Service/HIL and M4 remote frontend paired builds: PASS
- Android H0/L2/integration guards: PASS
- Android runtime/runtime-api/Service-HIL/UI tests, Observer lint and
  Observer/Service-HIL APK builds: PASS
- runtime motion/wire semantic diff: zero
- upload/device/HIL: NOT RUN (outside this software closure)

