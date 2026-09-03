---
authority: L0_EXECUTION
decision_state: approved
status: complete
completed_at: 2026-09-03
owner: product-owner-task-2026-09-03
---

# Control path failure attribution

Owner-approved scope: separate CSM-local readiness from Android telemetry
observation, fix steering-only terminal progress, and preserve bounded causal
path evidence. No motion semantics, watchdog threshold, queue/retry, or CAN
scheduler changes. No device write/HIL.

Flow: M4 executor/health -> existing IPC -> M7 local readiness -> independent
control ACK and telemetry sockets -> Android evidence projection. Existing
diagnostics are reused; a Service/HIL-only record carries bounded pipeline
counters and first-failure snapshots because existing health fills the fixed
512-byte wire maximum. No new runtime authority or worker is introduced.

Proof: deterministic source/native/Android tests, H0/L1/L2/L3/integration guards,
paired M7/M4 and Android builds. Physical first cause remains unproven until a
single correlated reproduction captures the local and transport boundaries.

## Closed defects and retained unknowns

- Android combined observation-stale/local-not-ready abort was incorrect.
  ACTIVE now treats telemetry loss as incomplete evidence, not a local watchdog.
- All-lane counter comparison hid steering stall while drive progressed.
  Only steering terminal progress resets the steering progress baseline.
- Android independently recomputed readiness from M4 flags and omitted M7's
  local age verdict. Record 26 schema 5 exports the unchanged local predicate.
- Control ACK admission, partial socket write, complete socket acceptance and
  Android receipt were not independently observable. Bounded record 27 and
  Android first-stop/write/RX evidence close that software evidence gap.
- No physical Wi-Fi, M4, IPC or common-runtime root cause was established.
  The Android string alone cannot establish any of those first boundaries.

Startup requires paired CISD/schema-5 images and the generated Android consumer.
Normal operation retains existing watchdogs, causal ACK and authority owners.
Observation gaps do not stop ACTIVE; fresh local fault reports still veto.
CSM first observations survive DISARM/re-ARM/reconnect until boot, not reset.
Recovery never changes activation or automatic-resume rules. Unknown data does
not become PASS; an endurance observation gap remains evidence-incomplete.

## Executed software proof

All commands below returned exit 0 on the final affected source.

CSM (repository root):

- `python tools/verify_harness.py`
- `python firmware/csm/tools/product_invariant_guard.py`
- `python firmware/csm/tools/architecture_conformance_guard.py`
- `python firmware/csm/tools/experiment_guard.py`
- `python firmware/csm/tools/wifi_architecture_guard.py`
- `python firmware/csm/tools/verify_control_path_contract.py`
- Native PowerShell runners in `firmware/csm/tools/`:
  `run_remote_control_contract_test.ps1`,
  `run_wifi_control_plane_contract_test.ps1`,
  `run_wifi_isolation_contract_test.ps1`, `run_uplink_contract_test.ps1`.
- Python compilation of affected PC decoders/monitor; `git diff --check`.
- `platformio run -d firmware/csm -e portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi -e portenta_h7_m4_remote_frontend`.

Native proof includes all 4096 local flag combinations with the unchanged
500/501 ms boundary, first-observation retention across later errors and time
wrap, ACK admitted/partial/full TX distinction, reconnect retention, and the
new record's existing Diagnostic priority / Batchable delivery.

Android:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/verify_harness.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/verify_android_architecture.ps1`
- `python tools/verify_integration.py --csm-root <CSM> --android-root <Android>`
- From `app/`, explicit canonical `CSM_PROTOCOL_DIR`:
  `gradlew.bat :runtime-api:test :runtime:test :service-hil:test :ui:testDebugUnitTest :product:lintObserverDebug :product:assembleObserverDebug :product:assembleServiceHilDebug`.
- JUnit: runtime-api 3, runtime 108, Service/HIL 16, UI 19:
  **146 tests, zero failures/errors/skips**.
- Tests distinguish fresh local-not-ready, stale/disconnected/different-boot
  telemetry, control disconnect, steering-only stall, unsigned canonical
  diagnostic projection and failed/pending writes versus an earlier success.
- `git diff --check`.

The initial trace expansion failed the 64-byte slot assertion; the fix uses
existing padding and derives snapshot rejects, without weakening the assertion.
Final review assigned record 27 Diagnostic priority before the final paired
build. A build performed before the new wire guard existed had a different
source-manifest identity; final paired images both match the manifest below.

## Exact candidate artifacts

Input Git baselines were CSM `a91cbf89117dcdada763a60347956abbc5e96aba` and
Android `80d3acb0d86a1d10d1e26af783ed92fb2057bae6`. Builds were performed before
the closure commits, with dirty-worktree identity; the source digest, not an
old baseline SHA, identifies the firmware contents.

- Paired source SHA-256: `e297557745e6860692a2b46eafcfea75c5ba57416aa9169a4d423e275537812c` (156 manifest inputs).
- M7 runtime contract ID: `0x73C534ED275411DF`; RAM 216200 B, flash 365584 B.
- M4 runtime contract ID: `0x1B778D5176BE4103`; RAM 44576 B, flash 89296 B.
- M7 firmware.bin SHA-256: `4E7D1B5AC15C1B44B72C27FE2A89D7D734FFA1BA9F6386A98A134A6A5472929C`.
- M4 firmware.bin SHA-256: `4CB7972A8836FCCE7B86230204A0CEC251AE22C334B06C784DC6C77D6B27C85C`.
- Observer debug APK SHA-256: `04867AFDC2CB96D7BA3F8F55580E162BD11D2921E150C4E00EB96275EE320DD3`.
- Service/HIL debug APK SHA-256: `23AF690C0588CABD354EF8A8C09AE416A9428CBD48E2FB05D699CC09498369CC`.

HAL volatile-cast, deprecated Android Wi-Fi API and native-symbol strip warnings
remain warnings; no claimed warning-free build. No vehicle bytes, polarity,
CAN schedule, transport capacity, watchdog or causal timeout was changed.
Diagnostic wire/schema changes are intentional and not a zero wire-layout diff.

## Physical gate

Upload, install, device, HIL, endurance soak and actual timing/allocation overhead
measurements: **NOT RUN**. Actual first failure and whether the two symptoms
share one physical/runtime cause remain unresolved, not software PASS claims.
Follow `docs/verification/CONTROL_PATH_FAILURE_ATTRIBUTION_KO.md`: same-run
record 26/27 + existing runtime/transport records, Android first-stop/write/ACK,
and independent USB/J4 evidence. No reset before recovering first snapshots.
Existing untracked root `artifacts/` was preserved and excluded from this commit.

## Exact changed files

CSM:

- `docs/architecture/ACTIVE_ARCHITECTURE.yaml`
- `docs/architecture/FIRMWARE_ARCHITECTURE_KO.md`
- `docs/exec-plans/completed/2026-09/CSM_CONTROL_PATH_FAILURE_ATTRIBUTION_20260903.md`
- `docs/verification/CONTROL_PATH_FAILURE_ATTRIBUTION_KO.md`
- `firmware/csm/include/board/control_island/ControlIslandContract.h`
- `firmware/csm/include/board/control_island/ControlPathDiagnostics.h`
- `firmware/csm/include/board/control_island/M4StaticCyclicExecutor.h`
- `firmware/csm/include/board/uplink/ProductUplinkEnvelope.h`
- `firmware/csm/include/board/uplink/WifiControlPlaneMailbox.h`
- `firmware/csm/include/board/uplink/WifiSocketWorker.h`
- `firmware/csm/include/board/uplink/WifiTcpSink.h`
- `firmware/csm/include/protocol/TypedFrame.h`
- `firmware/csm/include/protocol/TypedRecords.h`
- `firmware/csm/pc_tools/control_path_diagnostic.py`
- `firmware/csm/pc_tools/tcp_typed_monitor.py`
- `firmware/csm/pc_tools/verify_typed_stream.py`
- `firmware/csm/shared/docs/TRANSPORT_AND_RECORDS_KO.md`
- `firmware/csm/src/board/uplink/UplinkPriorityPolicy.cpp`
- `firmware/csm/src/board/uplink/WifiControlPlaneMailbox.cpp`
- `firmware/csm/src/board/uplink/WifiSocketWorker.cpp`
- `firmware/csm/src/m4_remote_frontend.cpp`
- `firmware/csm/src/main.cpp`
- `firmware/csm/test/remote_control_contract_test.cpp`
- `firmware/csm/test/uplink_contract_test.cpp`
- `firmware/csm/test/wifi_control_plane_contract_test.cpp`
- `firmware/csm/tools/architecture_conformance_guard.py`
- `firmware/csm/tools/verify_control_path_contract.py`

Android:

- `app/buildSrc/src/main/kotlin/com/hamt/vsm/build/GenerateCanonicalProtocolTask.kt`
- `app/product/src/serviceHil/kotlin/com/hamt/vsm/android/ProfileControlFactory.kt`
- `app/runtime-api/src/main/kotlin/com/hamt/vsm/runtime/api/ServiceControl.kt`
- `app/runtime/src/main/kotlin/com/hamt/vsm/runtime/HostControlPlane.kt`
- `app/runtime/src/main/kotlin/com/hamt/vsm/runtime/records/SessionCatalog.kt`
- `app/runtime/src/main/kotlin/com/hamt/vsm/runtime/session/ControlEvidenceReducer.kt`
- `app/runtime/src/test/kotlin/com/hamt/vsm/runtime/HostControlPlaneTest.kt`
- `app/runtime/src/test/kotlin/com/hamt/vsm/runtime/session/ControlContractReducerTest.kt`
- `app/service-hil/src/main/kotlin/com/hamt/vsm/servicehil/HeartbeatAdmissionTracker.kt`
- `app/service-hil/src/main/kotlin/com/hamt/vsm/servicehil/ServiceHilController.kt`
- `app/service-hil/src/test/kotlin/com/hamt/vsm/servicehil/DualAxisIntentTest.kt`
- `app/service-hil/src/test/kotlin/com/hamt/vsm/servicehil/ServiceHilGateTest.kt`
- `app/ui/src/main/kotlin/com/hamt/vsm/ui/ProductPages.kt`
- `app/ui/src/main/kotlin/com/hamt/vsm/ui/SteeringEndurancePage.kt`
- `docs/architecture/APP_RUNTIME_ARCHITECTURE_KO.md`
- `docs/architecture/EVIDENCE_CONTROL_CONTRACT_KO.md`
- `tools/verify_android_architecture.ps1`
