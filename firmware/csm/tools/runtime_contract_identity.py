#!/usr/bin/env python3
"""Stable identity for the selected PlatformIO runtime contract.

The source manifest answers "which firmware source tree?".  This module
answers the separate question "which resolved product/build contract from that
tree?" without using timestamps, Git dirty state, or host-specific build paths.
"""

from __future__ import annotations

import hashlib
import json
from typing import Any, Mapping


RUNTIME_CONTRACT_SCHEMA = 1
_SEQUENCE_FIELDS = (
    "frameworks",
    "source_filter",
    "build_flags",
    "build_unflags",
    "lib_deps",
    "lib_ignore",
    "platform_packages",
)
_SCALAR_FIELDS = (
    "pio_env",
    "platform",
    "board",
    "build_type",
    "lib_ldf_mode",
    "lib_compat_mode",
)
_BOARD_BUILD_OPTIONS = (
    "board_build.core",
    "board_build.variant",
    "board_build.mcu",
    "board_build.f_cpu",
    "board_build.ldscript",
)


def _normalize_text(value: Any) -> str:
    if value is None:
        return ""
    # Config values may contain Windows separators or CRLF even though their
    # meaning is host independent.  Do not case-fold: flags and paths can be
    # case sensitive on supported build hosts.
    return str(value).replace("\r\n", "\n").replace("\r", "\n").replace("\\", "/").strip()


def _normalize_sequence(value: Any) -> list[str]:
    if value is None:
        return []
    if isinstance(value, str):
        candidates = value.splitlines() if "\n" in value or "\r" in value else [value]
    else:
        try:
            candidates = list(value)
        except TypeError:
            candidates = [value]
    normalized = []
    for candidate in candidates:
        text = _normalize_text(candidate)
        if text:
            normalized.append(text)
    return normalized


def normalize_runtime_contract(values: Mapping[str, Any]) -> dict[str, Any]:
    """Return the whitelisted, canonical runtime-affecting contract."""
    contract: dict[str, Any] = {"schema": RUNTIME_CONTRACT_SCHEMA}
    for field in _SCALAR_FIELDS:
        contract[field] = _normalize_text(values.get(field))
    for field in _SEQUENCE_FIELDS:
        contract[field] = _normalize_sequence(values.get(field))

    board_build = values.get("board_build") or {}
    if not isinstance(board_build, Mapping):
        raise TypeError("board_build must be a mapping")
    contract["board_build"] = {
        _normalize_text(key): _normalize_text(value)
        for key, value in sorted(board_build.items(), key=lambda item: str(item[0]))
        if _normalize_text(value)
    }
    return contract


def build_runtime_contract_identity(values: Mapping[str, Any]) -> dict[str, Any]:
    """Hash a normalized runtime contract without volatile build metadata."""
    contract = normalize_runtime_contract(values)
    canonical = json.dumps(
        contract,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    digest = hashlib.sha256(b"CSM_RUNTIME_CONTRACT_V1\0" + canonical).hexdigest()
    return {
        "schema": RUNTIME_CONTRACT_SCHEMA,
        "runtime_contract_hash": digest,
        "runtime_contract_id32": int(digest[:8], 16),
        "runtime_contract_id64": int(digest[:16], 16),
        "contract": contract,
    }


def _project_option(env: Any, name: str, default: Any = "") -> Any:
    try:
        return env.GetProjectOption(name, default)
    except Exception:
        return default


def collect_platformio_runtime_contract(env: Any) -> dict[str, Any]:
    """Collect resolved, stable inputs from a PlatformIO/SCons environment."""
    board_build = {
        option: _project_option(env, option, "")
        for option in _BOARD_BUILD_OPTIONS
    }
    return normalize_runtime_contract(
        {
            "pio_env": env.get("PIOENV", ""),
            "platform": env.get("PIOPLATFORM", _project_option(env, "platform", "")),
            "board": env.get("BOARD", _project_option(env, "board", "")),
            "frameworks": _project_option(env, "framework", env.get("PIOFRAMEWORK", [])),
            "build_type": env.get("BUILD_TYPE", _project_option(env, "build_type", "release")),
            # These SCons values contain the fully inherited/resolved order.
            "source_filter": env.get("SRC_FILTER", _project_option(env, "build_src_filter", [])),
            "build_flags": env.get("BUILD_FLAGS", _project_option(env, "build_flags", [])),
            "build_unflags": env.get("BUILD_UNFLAGS", _project_option(env, "build_unflags", [])),
            "lib_deps": _project_option(env, "lib_deps", []),
            "lib_ignore": _project_option(env, "lib_ignore", []),
            "lib_ldf_mode": _project_option(env, "lib_ldf_mode", ""),
            "lib_compat_mode": _project_option(env, "lib_compat_mode", ""),
            "platform_packages": _project_option(env, "platform_packages", []),
            "board_build": board_build,
        }
    )


def build_platformio_runtime_contract_identity(env: Any) -> dict[str, Any]:
    return build_runtime_contract_identity(collect_platformio_runtime_contract(env))

