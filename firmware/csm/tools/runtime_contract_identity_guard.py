#!/usr/bin/env python3
"""Guard stable, material PlatformIO runtime contract identity."""

from __future__ import annotations

from pathlib import Path
import sys


IS_SCONS_SCRIPT = "Import" in globals()
if IS_SCONS_SCRIPT:
    Import("env")
    PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
else:
    PROJECT_DIR = Path(__file__).resolve().parents[1]

TOOLS_DIR = PROJECT_DIR / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from runtime_contract_identity import (
    build_platformio_runtime_contract_identity,
    build_runtime_contract_identity,
)


def _host_contract_guard() -> list[str]:
    errors: list[str] = []
    baseline = {
        "pio_env": "product",
        "platform": "ststm32",
        "board": "portenta_h7_m7",
        "frameworks": ["arduino"],
        "build_type": "release",
        "source_filter": ["+<*>", "-<WifiDisabled.cpp>"],
        "build_flags": ["-D BOARD_ENABLE_WIFI_UPLINK=1", "-D PRODUCT=1"],
        "build_unflags": [],
        "lib_deps": ["example.invalid/library.git#0123456789abcdef"],
        "lib_ignore": ["RPC"],
        "lib_ldf_mode": "chain+",
        "lib_compat_mode": "strict",
        "platform_packages": ["framework-arduino-mbed@4.2.1"],
        "board_build": {"board_build.f_cpu": "480000000L"},
    }
    first = build_runtime_contract_identity(baseline)
    repeated = build_runtime_contract_identity(dict(baseline))
    if first != repeated:
        errors.append("unchanged contract did not produce a stable identity")

    volatile = dict(baseline)
    volatile.update(
        {
            "build_epoch": 9999999999,
            "git_dirty": 1,
            "git_sha": "different",
            "host_path": "D:/another/worktree",
        }
    )
    if build_runtime_contract_identity(volatile)["runtime_contract_hash"] != first[
        "runtime_contract_hash"
    ]:
        errors.append("volatile build metadata changed runtime contract identity")

    windows_paths = dict(baseline)
    windows_paths["source_filter"] = ["+<*>", "-<WifiDisabled.cpp>"]
    if build_runtime_contract_identity(windows_paths)["runtime_contract_hash"] != first[
        "runtime_contract_hash"
    ]:
        errors.append("equivalent path separators changed runtime contract identity")

    material_mutations = {
        "PIO env": {"pio_env": "reset_ref"},
        "board": {"board": "portenta_h7_m4"},
        "framework": {"frameworks": ["mbed"]},
        "source filter": {"source_filter": ["+<*>", "+<Wifi.cpp>"]},
        "build flag": {"build_flags": ["-D BOARD_ENABLE_WIFI_UPLINK=0", "-D PRODUCT=1"]},
        "dependency": {"lib_deps": ["example.invalid/library.git#fedcba9876543210"]},
    }
    for label, mutation in material_mutations.items():
        changed = dict(baseline)
        changed.update(mutation)
        if build_runtime_contract_identity(changed)["runtime_contract_hash"] == first[
            "runtime_contract_hash"
        ]:
            errors.append(f"material {label} change did not change runtime identity")

    if first["runtime_contract_id32"] != int(
        first["runtime_contract_hash"][:8], 16
    ):
        errors.append("runtime contract ID32 is not the SHA-256 high 32 bits")
    if first["runtime_contract_id64"] != int(
        first["runtime_contract_hash"][:16], 16
    ):
        errors.append("runtime contract ID64 is not the SHA-256 high 64 bits")
    return errors


def _cpp_define_map(environment) -> dict[str, object]:
    result: dict[str, object] = {}
    for define in environment.get("CPPDEFINES", []):
        if isinstance(define, tuple) and len(define) == 2:
            result[str(define[0])] = define[1]
        elif isinstance(define, str):
            result[define] = None
    return result


def run_guard(environment=None) -> int:
    errors = _host_contract_guard()
    identity = None
    if environment is not None:
        identity = build_platformio_runtime_contract_identity(environment)
        contract = identity["contract"]
        for required in ("pio_env", "platform", "board"):
            if not contract[required]:
                errors.append(f"selected PlatformIO contract has empty {required}")
        if not contract["frameworks"]:
            errors.append("selected PlatformIO contract has no framework")

        defines = _cpp_define_map(environment)
        expected = {
            "CSM_FW_RUNTIME_CONTRACT_ID32":
                f"0x{identity['runtime_contract_id32']:08X}",
            "CSM_FW_RUNTIME_CONTRACT_ID64":
                f"0x{identity['runtime_contract_id64']:016X}ULL",
        }
        for name, value in expected.items():
            if defines.get(name) != value:
                errors.append(f"{name} macro does not match resolved contract")

    if errors:
        print("Runtime contract identity guard FAIL:")
        for error in errors:
            print(f"  - {error}")
        return 1
    if identity is None:
        print("Runtime contract identity host guard PASS")
    else:
        print(
            "Runtime contract identity guard PASS: "
            f"env={identity['contract']['pio_env']} "
            f"id32=0x{identity['runtime_contract_id32']:08X} "
            f"id64=0x{identity['runtime_contract_id64']:016X}"
        )
    return 0


if IS_SCONS_SCRIPT:
    result = run_guard(env)
    if result:
        Exit(result)
elif __name__ == "__main__":
    raise SystemExit(run_guard())

