import datetime
import hashlib
import os
import subprocess

Import("env")


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
build_dt = datetime.datetime.now().astimezone()
build_epoch = int(build_dt.timestamp())
identity_material = f"{git_sha}|{dirty}|{env_name}|{build_epoch}"
build_id = int.from_bytes(hashlib.sha1(identity_material.encode("utf-8")).digest()[:4], "little")

env.Append(
    CPPDEFINES=[
        ("CSM_FW_GIT_SHA", _macro_string(git_sha)),
        ("CSM_FW_GIT_DIRTY", dirty),
        ("CSM_FW_ENV_NAME", _macro_string(env_name)),
        ("CSM_FW_BUILD_EPOCH", build_epoch),
        ("CSM_FW_BUILD_ID", f"0x{build_id:08X}"),
    ]
)
