# CSM Remote Definition Workspace

This workspace holds the product-definition material for adding RC remote-control capability
to the existing CSM platform.

## Document Priority

1. `CSM_REMOTE_AGENT_ROUTING_MATRIX_KO.md`  
   Start here for token-efficient task routing. It tells which documents, sections, and agents to use.

2. `CSM_REMOTE_PRODUCT_DEFINITION_KO.md`  
   Korean master product definition. This is the controlling document.

3. `CSM_REMOTE_DEVELOPMENT_HARNESS_AGENT_ARCHITECTURE_KO.md`  
   Development harness and agent architecture. Use this to keep design changes, history,
   module boundaries, and evidence discipline aligned during implementation.

4. `CSM_REMOTE_DECISION_LEDGER_KO.md`  
   ADR-style record of accepted, rejected, and superseded decisions.

5. `CSM_REMOTE_OPEN_DECISIONS_KO.md`  
   Open product/architecture decisions that must not be guessed or silently closed.

6. `CSM_REMOTE_CHANGE_HISTORY_KO.md`  
   Short chronological project history for handoff and context recovery.

7. `CSM_REMOTE_REQUIREMENT_TRACE_KO.md`  
   Requirement-to-code/test/evidence trace. Filled progressively during implementation.

8. `CSM_REMOTE_HARNESS_FINAL_AUDIT_KO.md`  
   Final audit of whether the current harness is operationally ready for industrial product development.

9. `CSM_REMOTE_REPOSITORY_SETUP_KO.md`  
   Git/subtree setup record and rules for updating or splitting the imported CSM firmware.

10. `CSM_REMOTE_PHASE0_HANDOFF_REVIEW_KO.md`  
   Phase 0 review that classifies the original handoff as accepted, modified, held, or superseded.

11. `CSM_REMOTE_SOURCE_LAYOUT_PROPOSAL_KO.md`  
   Proposed firmware source layout for Phase 1 skeleton implementation.

12. `CSM_REMOTE_PRODUCT_DEFINITION.md`  
   English engineering companion. Use it for implementation-oriented review, but the Korean master wins on conflict.

13. `CSM_REMOTE_AUTHORITY_FINAL_HANDOFF.md`  
   Original handoff/background document. Useful context, but superseded by the product definition on conflict.

## Current Baseline

```text
Imported CSM subtree: firmware/csm
Original CSM upstream: C:\Users\JEON0295\Documents\PlatformIO\Projects\J_ArdP7_AM2_CSM
CSM commit: bfef287 Finalize passive CSM fault hold evidence
VSM: C:\WORKS\VS\turn81_full_buildfix2
Target hardware: Portenta H7 + Mid Carrier + Radiolink T16D + R16SM
```

## Firmware Path

```text
CSM PlatformIO project: firmware/csm
```


## Core Rule

```text
No confirmed autonomy release = zero local CAN TX.
```
