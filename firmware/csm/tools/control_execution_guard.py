#!/usr/bin/env python3
"""Architecture-independent executable checks for the Product Constitution."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PROJECT = ROOT / "firmware" / "csm"


def fail(message: str) -> None:
    raise SystemExit(f"Constitution guard failed: {message}")


constitution = (
    ROOT / "docs" / "product" / "PRODUCT_CONSTITUTION_KO.md"
).read_text(encoding="utf-8")
manifest = (
    ROOT / "docs" / "architecture" / "ACTIVE_ARCHITECTURE.yaml"
).read_text(encoding="utf-8")
main = (PROJECT / "src" / "main.cpp").read_text(encoding="utf-8")
owner = (
    PROJECT / "src" / "board" / "can" / "BuiltinCanTxOwner.cpp"
).read_text(encoding="utf-8")
platformio = (PROJECT / "platformio.ini").read_text(encoding="utf-8")

required_l1 = (
    "단일 권한·hard-safety",
    "physical execution owner가 정확히 하나",
    "fail closed",
    "stale",
    "admission truth와 physical-execution truth",
    "hardware evidence",
    "hidden retry",
    "무제한 control backlog",
    "telemetry sink 실패가 safety/control을 막지",
    "Production observer와 Service/HIL control profile",
    "semantic owner가 정확히 하나",
    "loss, drop, reset, reconnect, session transition",
    "experiment 값은 자동으로 production policy",
    "exploratory measurement와 reviewed value decision",
    "실행하지 않은 build, device, HIL",
)
for token in required_l1:
    if token not in constitution:
        fail(f"missing L1 invariant marker: {token}")

for token in (
    "host_software_execution_backlog: false",
    "hidden_retry: false",
    "hidden_replay: false",
    "production_vsm_control: false",
    "service_hil_control: true",
    "admission_equals_physical_success: false",
    "physical_success_requires_hardware_evidence: true",
    "production_authority: false",
):
    if token not in manifest:
        fail(f"active manifest contradicts/omits Constitution marker: {token}")

if main.count("builtin_can_ref().write(") != 1:
    fail("effective built-in CAN writer count is not one")
if "completion.transmitted()" not in main or "emit_can_tx_raw(" not in main:
    fail("physical-success evidence is not hardware-completion gated")
if "emit_host_control_tx_evidence(" not in main:
    fail("terminal outcome evidence boundary is missing")
if "tracking_fault_latched_ = true;" not in owner:
    fail("unresolved hardware ownership can fail open")
if "BOARD_CSM_PROFILE_FULL_INSTRUMENTED=1" not in platformio:
    fail("explicit Service/HIL profile boundary is missing")

print("Constitution guard PASS")
