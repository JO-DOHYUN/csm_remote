#!/usr/bin/env python3
"""Guard the one-variable CSM reset experiment build matrix."""

from __future__ import annotations

import configparser
import fnmatch
from pathlib import Path
import re


BASE_SECTION = "env:portenta_h7_m7_mid_j4_remote_product_reset_experiment_base"
PRODUCT_SECTION = "env:portenta_h7_m7_mid_mcp2515_j4_remote_product_wifi"
PROFILE_SECTIONS = {
    "env:portenta_h7_m7_mid_j4_remote_product_reset_experiment_ref": 0,
    "env:portenta_h7_m7_mid_j4_remote_product_reset_experiment_a": 1,
    "env:portenta_h7_m7_mid_j4_remote_product_reset_experiment_b": 2,
    "env:portenta_h7_m7_mid_j4_remote_product_reset_experiment_c": 3,
}
LEGACY_SECTIONS = (
    "env:portenta_h7_m7_mid_j4_remote_product_wifi_no_mcp_diag",
    "env:portenta_h7_m7_mid_j4_remote_product_wifi_no_can_tx_diag",
    "env:portenta_h7_m7_mid_mcp2515_j4_remote_product_wifi_runtime_diag",
    "env:portenta_h7_m7_mid_j4_remote_product_wifi_no_mcp_runtime_diag",
    "env:portenta_h7_m7_mid_j4_remote_product_wifi_no_can_tx_runtime_diag",
)
SELECTOR_DEFINE = "BOARD_RESET_EXPERIMENT_PROFILE"
REFERENCE_RE = re.compile(r"\$\{([^{}]+)\.([A-Za-z0-9_]+)\}")
SOURCE_FILTER_RE = re.compile(r"^([+-])<(.+)>$")


def _lines(value: str) -> list[str]:
    return [line.strip() for line in value.splitlines() if line.strip()]


def _parents(parser: configparser.RawConfigParser, section: str) -> list[str]:
    if not parser.has_section(section):
        return []
    return _lines(parser.get(section, "extends", raw=True, fallback=""))


def _resolve_option(
    parser: configparser.RawConfigParser,
    section: str,
    option: str,
    stack: tuple[tuple[str, str], ...] = (),
) -> str:
    key = (section, option)
    if key in stack:
        chain = " -> ".join(f"{name}.{field}" for name, field in (*stack, key))
        raise ValueError(f"cyclic PlatformIO interpolation: {chain}")
    next_stack = (*stack, key)

    if parser.has_section(section) and parser.has_option(section, option):
        raw_value = parser.get(section, option, raw=True)
    else:
        raw_value = ""
        for parent in _parents(parser, section):
            candidate = _resolve_option(parser, parent, option, next_stack)
            if candidate:
                raw_value = candidate
                break
        if not raw_value and section.startswith("env:") and parser.has_section("env"):
            raw_value = parser.get("env", option, raw=True, fallback="")

    def replace_reference(match: re.Match[str]) -> str:
        return _resolve_option(parser, match.group(1), match.group(2), next_stack)

    previous = None
    resolved = raw_value
    while resolved != previous and REFERENCE_RE.search(resolved):
        previous = resolved
        resolved = REFERENCE_RE.sub(replace_reference, resolved)
    return resolved


def _defines(flag_lines: list[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in flag_lines:
        if not line.startswith("-D"):
            continue
        define = line[2:].strip()
        if not define:
            continue
        name, separator, value = define.partition("=")
        result[name.strip()] = value.strip() if separator else "1"
    return result


def _source_is_selected(filter_lines: list[str], relative_path: str) -> bool:
    selected = False
    for line in filter_lines:
        match = SOURCE_FILTER_RE.match(line)
        if not match:
            continue
        if fnmatch.fnmatchcase(relative_path, match.group(2)):
            selected = match.group(1) == "+"
    return selected


def _without_selector(flag_lines: list[str]) -> tuple[str, ...]:
    selector_prefixes = (f"-D {SELECTOR_DEFINE}=", f"-D{SELECTOR_DEFINE}=")
    return tuple(
        line for line in flag_lines if not line.startswith(selector_prefixes)
    )


def _validate_header(root: Path, errors: list[str]) -> None:
    header = root / "include" / "board" / "diagnostics" / "ResetExperimentProfile.h"
    if not header.is_file():
        errors.append("missing include/board/diagnostics/ResetExperimentProfile.h")
        return
    text = header.read_text(encoding="utf-8")
    rows = re.findall(
        r"\{(\d+)u,\s*(true|false),\s*"
        r"ResetExperimentWifiRuntimeMode::(Off|ApOnly|Full),\s*\"([^\"]+)\"\}",
        text,
    )
    expected = [
        ("0", "true", "Full", "REF"),
        ("1", "true", "Off", "A"),
        ("2", "false", "Full", "B"),
        ("3", "true", "ApOnly", "C"),
    ]
    if rows != expected:
        errors.append(
            "ResetExperimentProfile.h truth table must be "
            "REF(on/full), A(on/off), B(off/full), C(on/AP-only)"
        )


def validate_reset_experiment_matrix(project_dir: Path) -> list[str]:
    root = Path(project_dir).resolve()
    parser = configparser.RawConfigParser()
    parser.optionxform = str
    parser.read(root / "platformio.ini", encoding="utf-8")
    errors: list[str] = []

    for section in LEGACY_SECTIONS:
        if parser.has_section(section):
            errors.append(f"legacy/no-op diagnostic environment still exists: {section}")

    if not parser.has_section(BASE_SECTION):
        errors.append(f"missing reset experiment base: {BASE_SECTION}")
        return errors
    if _parents(parser, BASE_SECTION) != [PRODUCT_SECTION]:
        errors.append(f"{BASE_SECTION} must extend only {PRODUCT_SECTION}")

    try:
        base_flag_lines = _lines(_resolve_option(parser, BASE_SECTION, "build_flags"))
        base_filter_lines = _lines(
            _resolve_option(parser, BASE_SECTION, "build_src_filter")
        )
    except ValueError as exc:
        errors.append(str(exc))
        return errors

    base_defines = _defines(base_flag_lines)
    required_base_defines = {
        "BOARD_ENABLE_WIFI_UPLINK": "1",
        "BOARD_ENABLE_RUNTIME_DIAGNOSTICS": "1",
        "BOARD_RUNTIME_DIAGNOSTIC_PERIOD_MS": "100",
        "BOARD_RUNTIME_DIAGNOSTIC_TX_OUTCOME_TIMEOUT_US": "5000",
        "BOARD_DIAG_SUPPRESS_REMOTE_CAN_TX": "1",
    }
    for name, expected in required_base_defines.items():
        actual = base_defines.get(name)
        if actual != expected:
            errors.append(f"{BASE_SECTION}: {name} must resolve to {expected}, got {actual}")
    if SELECTOR_DEFINE in base_defines:
        errors.append(f"{BASE_SECTION} must not set {SELECTOR_DEFINE}")

    required_wifi_sources = (
        "board/uplink/WifiTcpSink.cpp",
        "board/uplink/WifiSocketWorker.cpp",
        "board/uplink/WifiWorkerMailbox.cpp",
    )
    for source in required_wifi_sources:
        if not _source_is_selected(base_filter_lines, source):
            errors.append(f"{BASE_SECTION} does not link required Wi-Fi source: {source}")
    if _source_is_selected(base_filter_lines, "board/uplink/WifiTcpSinkDisabled.cpp"):
        errors.append(f"{BASE_SECTION} links the disabled Wi-Fi implementation")

    reference_nonselector_flags: tuple[str, ...] | None = None
    reference_source_filter: tuple[str, ...] | None = None
    for section, selector in PROFILE_SECTIONS.items():
        if not parser.has_section(section):
            errors.append(f"missing reset experiment profile: {section}")
            continue
        if _parents(parser, section) != [BASE_SECTION]:
            errors.append(f"{section} must extend only {BASE_SECTION}")
        if parser.has_option(section, "build_src_filter"):
            errors.append(f"{section} must inherit, not override, build_src_filter")

        direct_flags = _lines(parser.get(section, "build_flags", raw=True, fallback=""))
        expected_direct_flags = [
            f"${{{BASE_SECTION}.build_flags}}",
            f"-D {SELECTOR_DEFINE}={selector}",
        ]
        if direct_flags != expected_direct_flags:
            errors.append(
                f"{section} build_flags must contain only base flags plus "
                f"{SELECTOR_DEFINE}={selector}"
            )

        try:
            resolved_flags = _lines(_resolve_option(parser, section, "build_flags"))
            resolved_filter = tuple(
                _lines(_resolve_option(parser, section, "build_src_filter"))
            )
        except ValueError as exc:
            errors.append(str(exc))
            continue
        defines = _defines(resolved_flags)
        if defines.get(SELECTOR_DEFINE) != str(selector):
            errors.append(
                f"{section}: {SELECTOR_DEFINE} must resolve to {selector}"
            )

        nonselector_flags = _without_selector(resolved_flags)
        if reference_nonselector_flags is None:
            reference_nonselector_flags = nonselector_flags
            reference_source_filter = resolved_filter
        else:
            if nonselector_flags != reference_nonselector_flags:
                errors.append(f"{section}: non-selector build flags differ from REF")
            if resolved_filter != reference_source_filter:
                errors.append(f"{section}: source filter differs from REF")

    _validate_header(root, errors)
    return errors


def run_guard(project_dir: Path, active_env: str = "") -> int:
    errors = validate_reset_experiment_matrix(project_dir)
    if active_env == BASE_SECTION.removeprefix("env:"):
        errors.append("the unlabeled reset experiment base is not a build target; use REF/A/B/C")
    if errors:
        print("Reset experiment profile guard FAIL:")
        for error in errors:
            print(f"  - {error}")
        return 1
    print("Reset experiment profile guard PASS: REF/A/B/C differ only by selector")
    return 0


if "Import" in globals():
    Import("env")
    result = run_guard(
        Path(env.subst("$PROJECT_DIR")), active_env=env.subst("$PIOENV")
    )
    if result:
        Exit(result)
elif __name__ == "__main__":
    raise SystemExit(run_guard(Path(__file__).resolve().parents[1]))
