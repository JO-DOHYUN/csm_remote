import datetime
import hashlib
from pathlib import Path
import subprocess
import sys

Import("env")

TOOLS_DIR = Path(env.subst("$PROJECT_DIR")) / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from source_manifest import build_source_manifest
from runtime_contract_identity import build_platformio_runtime_contract_identity


def _run_git(args):
    try:
        return subprocess.check_output(
            ["git", *args],
            cwd=env["PROJECT_DIR"],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except Exception:
        return ""


def _macro_string(value):
    escaped = str(value).replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


git_sha = _run_git(["rev-parse", "--short=12", "HEAD"]) or "unknown"
status = _run_git(["status", "--porcelain", "--untracked-files=all"])
dirty = 1 if status else 0
env_name = env.get("PIOENV", "unknown")
source_manifest = build_source_manifest(Path(env["PROJECT_DIR"]))
source_hash = source_manifest["source_hash"]
source_id32 = source_manifest["source_id32"]
source_id64 = source_manifest["source_id64"]
runtime_contract = build_platformio_runtime_contract_identity(env)
runtime_contract_hash = runtime_contract["runtime_contract_hash"]
runtime_contract_id32 = runtime_contract["runtime_contract_id32"]
runtime_contract_id64 = runtime_contract["runtime_contract_id64"]
build_dt = datetime.datetime.now().astimezone()
build_epoch = int(build_dt.timestamp())
identity_material = f"{git_sha}|{dirty}|{env_name}|{build_epoch}"
build_id = int.from_bytes(hashlib.sha1(identity_material.encode("utf-8")).digest()[:4], "little")

env.Append(
    CPPDEFINES=[
        ("CSM_FW_GIT_SHA", _macro_string(git_sha)),
        ("CSM_FW_GIT_DIRTY", dirty),
        ("CSM_FW_ENV_NAME", _macro_string(env_name)),
        ("CSM_FW_SOURCE_HASH", _macro_string(source_hash)),
        ("CSM_FW_SOURCE_ID32", f"0x{source_id32:08X}"),
        ("CSM_FW_SOURCE_ID64", f"0x{source_id64:016X}ULL"),
        ("CSM_FW_RUNTIME_CONTRACT_HASH", _macro_string(runtime_contract_hash)),
        ("CSM_FW_RUNTIME_CONTRACT_ID32", f"0x{runtime_contract_id32:08X}"),
        ("CSM_FW_RUNTIME_CONTRACT_ID64", f"0x{runtime_contract_id64:016X}ULL"),
        ("CSM_FW_BUILD_EPOCH", build_epoch),
        ("CSM_FW_BUILD_ID", f"0x{build_id:08X}"),
    ]
)
