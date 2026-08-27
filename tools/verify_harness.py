#!/usr/bin/env python3
"""Deterministic Harness V2 route and authority verifier."""

from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "firmware" / "csm"

REQUIRED = (
    "AGENTS.md",
    "START_HERE_KO.md",
    "CURRENT.md",
    "INDEX.md",
    "docs/product/PRODUCT_CONSTITUTION_KO.md",
    "docs/product/PRODUCT_DEFINITION_KO.md",
    "docs/product/OPERATING_SCENARIOS_KO.md",
    "docs/architecture/ACTIVE_ARCHITECTURE.yaml",
    "docs/architecture/FIRMWARE_ARCHITECTURE_KO.md",
    "docs/architecture/UPLINK_TRANSPORT_ARCHITECTURE_KO.md",
    "docs/experiments/host_threshold_qualification.yaml",
    "docs/quality/VERIFICATION_POLICY_KO.md",
    "docs/operations/DEVELOPMENT_SETUP_KO.md",
    "history/decisions/ACTIVE_INDEX.md",
    "history/decisions/DECISION_LEDGER_KO.md",
    "firmware/csm/platformio.ini",
    "firmware/csm/tools/control_execution_guard.py",
    "firmware/csm/tools/architecture_conformance_guard.py",
    "firmware/csm/tools/experiment_guard.py",
    "firmware/csm/shared/docs/TRANSPORT_AND_RECORDS_KO.md",
)

EXPECTED_SKILLS = {
    "implement",
    "architecture-change",
    "experiment",
    "verification",
    "harness-maint",
    "embedded-platformio",
    "can-hil",
}

EXPECTED_ENVS = {
    "portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi",
    "portenta_h7_m4_remote_frontend",
}

MANIFEST_REQUIRED = {
    "authority_level": "L2",
    "architecture_id": "csm-hno1-m4-control-island-rev-b",
    "physical_can_owner": "csm_m4_control_island",
    "host_semantic_owner": "android_vsm",
    "remote_semantic_owner": "csm_m7",
    "global_source_authority_owner": "csm_m7",
    "nominal_request_clock_owner": "csm_m4_tim4",
    "fdcan1_owner": "csm_m4_control_island",
    "host_execution_model": "coherent_latest_state",
    "latest_state_depth": "1",
    "host_software_execution_backlog": "false",
    "hidden_retry": "false",
    "hidden_replay": "false",
    "source_mixing": "forbidden",
    "authority_handoff": "quiescent_cancel_terminal_then_activate",
    "strict_n_shot_owner": "csm_m4_generic_success_budget",
    "control_slots": "2",
    "integrity": "sequence_crc32_boot_identity",
    "raw_can_ring_capacity": "512",
    "production_vsm_control": "false",
    "service_hil_control": "true",
    "admission_equals_physical_success": "false",
    "physical_success_requires_hardware_evidence": "true",
    "threshold_state": "exploratory",
    "production_authority": "false",
}


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


for relative in REQUIRED:
    if not (ROOT / relative).is_file():
        fail(f"missing required file: {relative}")

for forbidden in (
    "BRIEF.md",
    "docs/roadmap",
    "firmware/csm/docs/decisions",
    "firmware/csm/docs/quality",
    "firmware/csm/.github",
):
    if (ROOT / forbidden).exists():
        fail(f"parallel/stale active route remains: {forbidden}")

agents = read("AGENTS.md")
current = read("CURRENT.md")
if len(agents.splitlines()) > 150:
    fail("AGENTS.md exceeds the 150-line stable-map target")
if len(current.splitlines()) > 90:
    fail("CURRENT.md exceeds the short current-state boundary")

default_route = "\n".join(
    read(path)
    for path in ("AGENTS.md", "START_HERE_KO.md", "CURRENT.md", "README.md")
)
for stale in (
    "BRIEF.md",
    "product-change-review",
    "remote_phase1_guard",
    "portenta_h7_m7_mid_mcp2515_j4_dual_csm",
    "portenta_h7_m7_mid_mcp2515_j4_remote_product_wifi",
):
    if stale in default_route:
        fail(f"stale default route remains: {stale}")
if "DECISION_LEDGER_KO.md" in default_route:
    fail("giant decision ledger leaked into default context")

skill_root = ROOT / ".agents" / "skills"
skill_dirs = {path.name for path in skill_root.iterdir() if path.is_dir()}
if skill_dirs != EXPECTED_SKILLS:
    fail(f"skill set mismatch: {sorted(skill_dirs)}")
names: dict[str, Path] = {}
for path in sorted(skill_root.glob("*/SKILL.md")):
    match = re.search(r"(?m)^name:\s*(\S+)\s*$", path.read_text(encoding="utf-8"))
    if not match:
        fail(f"skill missing frontmatter name: {path.relative_to(ROOT)}")
    name = match.group(1)
    if name in names:
        fail(f"duplicate skill name {name}: {names[name]} and {path}")
    names[name] = path
if set(names) != EXPECTED_SKILLS:
    fail(f"skill frontmatter mismatch: {sorted(names)}")

manifest_text = read("docs/architecture/ACTIVE_ARCHITECTURE.yaml")
manifest: dict[str, str] = {}
for line in manifest_text.splitlines():
    match = re.match(r"^\s*([a-z0-9_]+):\s*(\S.*?)\s*$", line)
    if match:
        manifest[match.group(1)] = match.group(2)
for key, expected in MANIFEST_REQUIRED.items():
    if manifest.get(key) != expected:
        fail(f"manifest {key}={manifest.get(key)!r}, expected {expected!r}")
for sha in (
    "5c9f6f71ebb06320f8835c4115ddde03314a9762",
    "fea18d9d5660d96cfa67e93e7e900ef9cceb62a6",
):
    if sha not in manifest_text or sha not in current:
        fail(f"baseline is inconsistent between manifest and CURRENT: {sha}")

if "L2 ACTIVE ARCHITECTURE" not in read(
    "docs/architecture/FIRMWARE_ARCHITECTURE_KO.md"
):
    fail("firmware architecture is not classified as L2")
if "L2 DOMAIN ARCHITECTURE" not in read(
    "docs/architecture/UPLINK_TRANSPORT_ARCHITECTURE_KO.md"
):
    fail("uplink architecture is not classified as L2")

constitution = read("docs/product/PRODUCT_CONSTITUTION_KO.md")
if len(re.findall(r"(?m)^\d+\.", constitution)) != 15:
    fail("Product Constitution must contain exactly 15 numbered invariants")
product_scope = read("docs/product/PRODUCT_DEFINITION_KO.md") + read(
    "docs/product/OPERATING_SCENARIOS_KO.md"
)
for leaked in ("M4", "M7", "TIM4", "3-slot", "5000 us", "BuiltinCanTxOwner"):
    if leaked in product_scope:
        fail(f"current implementation leaked into product/scenario authority: {leaked}")

if "HISTORY / NON-AUTHORITATIVE" not in read(
    "history/decisions/DECISION_LEDGER_KO.md"
):
    fail("decision ledger is not classified as HISTORY")
snapshot = ROOT / "history" / "snapshots" / "BRIEF_20260819_KO.md"
if not snapshot.is_file() or "HISTORY SNAPSHOT" not in snapshot.read_text(encoding="utf-8"):
    fail("former BRIEF was not preserved as non-authoritative HISTORY")

platformio = read("firmware/csm/platformio.ini")
envs = set(re.findall(r"(?m)^\[env:([^\]]+)\]$", platformio))
if envs != EXPECTED_ENVS:
    fail(f"active PlatformIO environments mismatch: {sorted(envs)}")

compile_db = PROJECT / "compile_commands.json"
if compile_db.exists():
    db = compile_db.read_text(encoding="utf-8", errors="ignore")
    active = "portenta_h7_m7_mid_feeder_uart_j4_remote_service_hil_wifi"
    if active not in db:
        fail("compile_commands.json is stale or not generated from active Service/HIL env")
    for stale in ("remote_product_mdps_bench", "dual_csm_passive"):
        if stale in db:
            fail(f"compile_commands.json contains stale environment: {stale}")

tick = chr(96)
route_pattern = re.compile(
    re.escape(tick) + r"((?:docs|history|firmware|tools|\.agents)/[^" + tick + r"]+)" + re.escape(tick)
)
for relative in (
    "AGENTS.md",
    "START_HERE_KO.md",
    "CURRENT.md",
    "INDEX.md",
    "README.md",
):
    text = read(relative)
    for match in route_pattern.finditer(text):
        route = match.group(1).split(" (", 1)[0]
        if "*" in route or route.endswith("/"):
            continue
        if not (ROOT / route).exists():
            fail(f"broken active route in {relative}: {route}")

checks = (
    ([sys.executable, "firmware/csm/tools/control_execution_guard.py"], ROOT),
    ([sys.executable, "firmware/csm/tools/architecture_conformance_guard.py"], ROOT),
    ([sys.executable, "firmware/csm/tools/experiment_guard.py"], ROOT),
    ([sys.executable, "tools/wifi_architecture_guard.py"], PROJECT),
    (["git", "diff", "--check"], ROOT),
)
for command, cwd in checks:
    result = subprocess.run(command, cwd=cwd, check=False, capture_output=True, text=True)
    if result.returncode:
        fail(
            f"check failed: {' '.join(command)}\n"
            f"{(result.stdout + result.stderr).strip()}"
        )

top = subprocess.run(
    ["git", "rev-parse", "--show-toplevel"],
    cwd=ROOT,
    check=True,
    capture_output=True,
    text=True,
).stdout.strip()
if Path(top).resolve() != ROOT:
    fail("workspace root is not the Git repository root")

print("PASS: CSM Harness V2 authority, routes, skills, manifest and guards")
