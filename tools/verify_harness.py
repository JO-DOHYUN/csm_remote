from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]

REQUIRED = (
    "AGENTS.md",
    "START_HERE_KO.md",
    "BRIEF.md",
    "INDEX.md",
    "docs/product/PRODUCT_DEFINITION_KO.md",
    "docs/product/OPERATING_SCENARIOS_KO.md",
    "docs/architecture/FIRMWARE_ARCHITECTURE_KO.md",
    "docs/architecture/UPLINK_TRANSPORT_ARCHITECTURE_KO.md",
    "docs/quality/VERIFICATION_POLICY_KO.md",
    "docs/operations/DEVELOPMENT_SETUP_KO.md",
    "history/decisions/DECISION_LEDGER_KO.md",
    "firmware/csm/platformio.ini",
    "firmware/csm/src/main.cpp",
    "firmware/csm/shared/docs/TRANSPORT_AND_RECORDS_KO.md",
)

FORBIDDEN = (
    "docs/remote",
    "firmware/csm/docs/ai_harness",
    "firmware/csm/CSM_03_architect_synthesis_codex_prompt.md",
    "firmware/csm/VMS_CSM_03_ARCHITECT_SYNTHESIS_FINAL.md",
)

EXPECTED_ENVS = (
    "portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive",
    "portenta_h7_m4_remote_frontend_build_proof",
    "portenta_h7_m4_remote_serial3_capture_probe",
)


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


for relative in REQUIRED:
    if not (ROOT / relative).is_file():
        fail(f"missing required file: {relative}")

for relative in FORBIDDEN:
    if (ROOT / relative).exists():
        fail(f"legacy path remains: {relative}")

agent_files = sorted(ROOT.rglob("AGENTS.md"))
if agent_files != [ROOT / "AGENTS.md"]:
    fail("AGENTS.md must exist only at workspace root")

platformio = (ROOT / "firmware/csm/platformio.ini").read_text(encoding="utf-8")
for env_name in EXPECTED_ENVS:
    if f"[env:{env_name}]" not in platformio:
        fail(f"missing PlatformIO environment: {env_name}")

active_docs = [
    ROOT / "AGENTS.md",
    ROOT / "START_HERE_KO.md",
    ROOT / "BRIEF.md",
    ROOT / "INDEX.md",
    *(ROOT / "docs").rglob("*.md"),
    *(ROOT / "history").rglob("*.md"),
]
stale_terms = ("docs/remote/", "board/AGENTS.md", "firmware/csm/AGENTS.md")
for path in active_docs:
    text = path.read_text(encoding="utf-8")
    for term in stale_terms:
        if term in text:
            fail(f"stale route '{term}' in {path.relative_to(ROOT)}")

result = subprocess.run(
    ["git", "rev-parse", "--show-toplevel"],
    cwd=ROOT,
    check=True,
    capture_output=True,
    text=True,
)
if Path(result.stdout.strip()).resolve() != ROOT:
    fail("workspace root is not the Git repository root")

print("PASS: CSM harness routes, authorities, and required build environments")
