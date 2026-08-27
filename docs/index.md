---
authority: PROCEDURE
owner: repository-route
status: active
read_when: affected owner is unclear
---

# CSM Authority Map

| Domain | Authority | Source / proof route |
|---|---|---|
| product invariants | `product/PRODUCT_CONSTITUTION_KO.md` | L1 guard |
| product purpose/scenarios | `product/` | affected source/tests |
| active firmware architecture | `architecture/ACTIVE_ARCHITECTURE.yaml` and affected L2 | L2 guard |
| typed wire | `../firmware/csm/shared/docs/TRANSPORT_AND_RECORDS_KO.md` | contract tests |
| verification policy | `verification/VERIFICATION_POLICY_KO.md` | affected proof procedure |
| current qualification evidence | `verification/qualification-status.yaml` | verification tasks only |
| experiments | `experiments/index.yaml` | L3 guard |
| cross-repo verified evidence | `integration/verified-baselines/` | explicit integration task only |
| large active work | `exec-plans/active/` | matching L0 task only |
| historical rationale | `../history/decisions/HISTORY_NAVIGATOR.md` | targeted search only |
| toolchain/build | `operations/DEVELOPMENT_SETUP_KO.md` | exact environment build |

`reference/`는 입력 자료, `generated/`는 provenance가 있는 비권위 생성물이다.
