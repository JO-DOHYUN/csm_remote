#!/usr/bin/env python3
"""Harness V3 H0 verifier: routes, metadata, context budget and repository hygiene."""

from pathlib import Path
import hashlib
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
PRIMARY = {"implement", "architecture-change", "experiment", "harness-maint"}
PROCEDURES = {"verification", "embedded-platformio", "can-hil"}
REQUIRED = (
    "AGENTS.md",
    "START_HERE_KO.md",
    "README.md",
    "docs/index.md",
    "docs/harness/HARNESS_V3_ARCHITECTURE_KO.md",
    "docs/verification/VERIFICATION_POLICY_KO.md",
    "docs/verification/qualification-status.yaml",
    "docs/experiments/index.yaml",
    "docs/integration/verified-baselines/harness-v2-20260821.yaml",
    "history/decisions/HISTORY_NAVIGATOR.md",
    "reference/README.md",
    "generated/README.md",
    "generated/can-db/provenance.yaml",
    "tools/test_harness_scenarios.py",
    "tools/fixtures/harness_v3_scenarios.json",
)


def fail(message: str) -> None:
    raise SystemExit(f"FAIL: {message}")


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


for relative in REQUIRED:
    if not (ROOT / relative).is_file():
        fail(f"missing required H0 file: {relative}")
for obsolete in (
    "CURRENT.md",
    "INDEX.md",
    "history/decisions/ACTIVE_INDEX.md",
    "docs/quality/VERIFICATION_POLICY_KO.md",
    "firmware/csm/tools/control_execution_guard.py",
    "firmware/csm/db_out",
):
    if (ROOT / obsolete).exists():
        fail(f"obsolete V2 route remains: {obsolete}")

agents = read("AGENTS.md")
if len(agents.splitlines()) > 100:
    fail("AGENTS.md exceeds the 100-line stable-map budget")

default_text = "\n".join(read(path) for path in ("AGENTS.md", "START_HERE_KO.md", "README.md"))
if "CURRENT.md" in default_text:
    fail("CURRENT leaked into default route")
if re.search(r"\b[0-9a-f]{40}\b", default_text, re.IGNORECASE):
    fail("commit SHA leaked into default route")
if re.search(r"(?i)\b[a-z]:\\", default_text):
    fail("absolute machine path leaked into default route")
if "targeted" not in agents or "Git state" not in agents:
    fail("context reconstruction/history isolation rule is missing")

nested_agents = [path for path in ROOT.rglob("AGENTS.md") if path != ROOT / "AGENTS.md"]
if nested_agents:
    fail(f"nested AGENTS files exceed the zero-file budget: {nested_agents}")

skill_root = ROOT / ".agents" / "skills"
seen: dict[str, str] = {}
for path in sorted(skill_root.glob("*/SKILL.md")):
    text = path.read_text(encoding="utf-8")
    name = re.search(r"(?m)^name:\s*(\S+)\s*$", text)
    kind = re.search(r"(?m)^kind:\s*(primary|procedure)\s*$", text)
    if not name or not kind:
        fail(f"skill metadata missing: {path.relative_to(ROOT)}")
    if name.group(1) != path.parent.name or name.group(1) in seen:
        fail(f"duplicate/mismatched skill: {path.relative_to(ROOT)}")
    seen[name.group(1)] = kind.group(1)
    if re.search(r"\bCURRENT(?:\.md)?\b", text):
        fail(f"skill depends on CURRENT: {path.relative_to(ROOT)}")
if {name for name, kind in seen.items() if kind == "primary"} != PRIMARY:
    fail("primary skill set mismatch")
if {name for name, kind in seen.items() if kind == "procedure"} != PROCEDURES:
    fail("verification procedure set mismatch")

for relative in (
    "docs/index.md",
    "docs/harness/HARNESS_V3_ARCHITECTURE_KO.md",
    "docs/verification/VERIFICATION_POLICY_KO.md",
):
    text = read(relative)
    for key in ("authority:", "owner:", "status:", "read_when:"):
        if key not in text:
            fail(f"{relative} missing metadata {key}")

for plan in (ROOT / "docs" / "exec-plans").rglob("*.yaml"):
    text = plan.read_text(encoding="utf-8")
    for key in ("id:", "status:", "decision_state:", "goal:", "scope:", "frozen:", "required:", "forbidden:", "proof:", "cross_repo:"):
        if key not in text:
            fail(f"exec-plan schema missing {key}: {plan.relative_to(ROOT)}")
    if "active" in plan.parts and "status: ACTIVE" not in text:
        fail(f"active plan status mismatch: {plan.relative_to(ROOT)}")
    if "completed" in plan.parts and "status: COMPLETED" not in text:
        fail(f"completed plan status mismatch: {plan.relative_to(ROOT)}")

for path in (ROOT / "firmware" / "csm").glob("*"):
    if path.suffix.lower() in {".xlsx", ".pdf", ".png"}:
        fail(f"reference asset remains in build source root: {path.relative_to(ROOT)}")
for path in (ROOT / "firmware" / "csm" / "pc_tools").glob("*.xlsx"):
    fail(f"duplicate reference asset remains in tool source: {path.relative_to(ROOT)}")

provenance = read("generated/can-db/provenance.yaml")
for output in sorted((ROOT / "generated" / "can-db").glob("*.json")):
    digest = hashlib.sha256(output.read_bytes()).hexdigest().upper()
    if f"{output.name}: {digest}" not in provenance:
        fail(f"generated output provenance mismatch: {output.name}")

route_files = [
    ROOT / "AGENTS.md",
    ROOT / "START_HERE_KO.md",
    ROOT / "README.md",
    ROOT / "docs" / "index.md",
    ROOT / "docs" / "harness" / "HARNESS_V3_ARCHITECTURE_KO.md",
]
route_files.extend(sorted(skill_root.glob("*/SKILL.md")))
route_pattern = re.compile(
    r"`((?:\.\./)?(?:docs|history|firmware|tools|\.agents|reference|generated|"
    r"product|architecture|verification|experiments|integration|exec-plans|operations)/[^`]+)`"
)
for source in route_files:
    for route in route_pattern.findall(source.read_text(encoding="utf-8")):
        route = route.split("#", 1)[0].rstrip("/")
        if "*" in route or " " in route:
            continue
        if route.startswith(("docs/", "history/", "firmware/", "tools/", ".agents/", "reference/", "generated/")):
            candidate = ROOT / route
        else:
            candidate = source.parent / route
        if not candidate.exists():
            fail(f"broken route in {source.relative_to(ROOT)}: {route}")

result = subprocess.run(["git", "diff", "HEAD", "--check"], cwd=ROOT, capture_output=True, text=True)
if result.returncode:
    fail(f"git diff --check failed:\n{result.stdout}{result.stderr}")

print("PASS: CSM Harness V3 H0 routes, metadata and context boundaries")
