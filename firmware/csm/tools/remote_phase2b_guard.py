from pathlib import Path
import configparser
import sys


M4_ENV = "env:portenta_h7_m4_remote_serial3_capture_probe"
PROBE_SOURCE = "m4_remote_serial3_capture_probe.cpp"
BUILD_PROOF_SOURCE = "m4_remote_frontend_build_proof.cpp"


def _read(path):
    return path.read_text(encoding="utf-8", errors="ignore")


def _section_lines(parser, section, option):
    value = parser.get(section, option, fallback="")
    return [line.strip() for line in value.splitlines() if line.strip()]


def _contains_any(text, patterns):
    return [pattern for pattern in patterns if pattern in text]


def main():
    csm_root = Path(__file__).resolve().parents[1]
    parser = configparser.RawConfigParser()
    parser.optionxform = str
    parser.read(csm_root / "platformio.ini", encoding="utf-8")

    errors = []
    if not parser.has_section(M4_ENV):
        errors.append(f"platformio.ini missing {M4_ENV}")
    else:
        if parser.get(M4_ENV, "board", fallback="") != "portenta_h7_m4":
            errors.append(f"{M4_ENV} must use board=portenta_h7_m4")

        src_filter = "\n".join(_section_lines(parser, M4_ENV, "build_src_filter"))
        required_sources = [
            f"+<{PROBE_SOURCE}>",
            "+<board/remote/RemoteTypes.cpp>",
            "+<board/remote/CrsfParser.cpp>",
            "+<board/remote/RcNormalizer.cpp>",
            "+<board/remote/M4RemoteMailboxContract.cpp>",
            "+<board/remote/M4RemoteMailboxWriter.cpp>",
        ]
        for required in required_sources:
            if required not in src_filter:
                errors.append(f"{M4_ENV} missing source filter {required}")

        forbidden_sources = [
            "+<main.cpp>",
            "+<board/control",
            "+<board/authority",
            "HostDownlink",
            "CanTxGateway",
            "VehicleCommandMapper",
            "RemoteControlOrchestrator",
            "M4RemoteMailboxReader",
        ]
        for forbidden in forbidden_sources:
            if forbidden in src_filter:
                errors.append(f"{M4_ENV} includes forbidden source {forbidden}")

        flags = "\n".join(_section_lines(parser, M4_ENV, "build_flags"))
        required_flags = [
            "-D BOARD_M4_REMOTE_SERIAL3_CAPTURE_PROBE=1",
            "-D BOARD_M4_REMOTE_CAPTURE_BAUD=420000UL",
            "-D BOARD_ENABLE_REMOTE_CONTROL=0",
            "-D BOARD_ENABLE_M4_REMOTE_FRONTEND=0",
            "-D BOARD_ENABLE_REMOTE_AUTHORITY=0",
            "-D BOARD_ENABLE_HOST_CAN_TX=0",
            "-D BOARD_ENABLE_HOST_DOWNLINK=0",
        ]
        for required in required_flags:
            if required not in flags:
                errors.append(f"{M4_ENV} missing build flag {required}")

        forbidden_flags = [
            "BOARD_ENABLE_REMOTE_CONTROL=1",
            "BOARD_ENABLE_M4_REMOTE_FRONTEND=1",
            "BOARD_ENABLE_REMOTE_AUTHORITY=1",
            "BOARD_ENABLE_HOST_CAN_TX=1",
            "BOARD_ENABLE_HOST_DOWNLINK=1",
        ]
        for forbidden in forbidden_flags:
            if forbidden in flags:
                errors.append(f"{M4_ENV} enables forbidden flag {forbidden}")

    for section in parser.sections():
        if section == M4_ENV:
            continue
        src_filter = "\n".join(_section_lines(parser, section, "build_src_filter"))
        if "+<*>" not in src_filter:
            continue
        if f"-<{PROBE_SOURCE}>" not in src_filter:
            errors.append(f"{section} must exclude {PROBE_SOURCE}")
        if f"-<{BUILD_PROOF_SOURCE}>" not in src_filter:
            errors.append(f"{section} must exclude {BUILD_PROOF_SOURCE}")

    probe_path = csm_root / "src" / PROBE_SOURCE
    if not probe_path.exists():
        errors.append(f"src/{PROBE_SOURCE}: missing file")
    else:
        probe_text = _read(probe_path)
        required_patterns = [
            "BOARD_M4_REMOTE_SERIAL3_CAPTURE_PROBE",
            "Serial3.begin",
            "Serial3.available",
            "Serial3.read",
            "decodeCrsfRcChannelsPacked",
            "normalizeCrsfChannels",
            "publishSample",
            "kRemoteRequiredRcChannelMask",
        ]
        for pattern in required_patterns:
            if pattern not in probe_text:
                errors.append(f"src/{PROBE_SOURCE}: missing required pattern {pattern}")

        forbidden_patterns = [
            "Serial.begin",
            "Serial.print",
            "Serial.println",
            "Serial1",
            "Serial2",
            "Serial4",
            "pinMode",
            "digitalWrite",
            "analogWrite",
            "HSEM",
            "OpenAMP",
            "RPC",
            "CAN::write",
            "submitTx",
            "sendMessage",
            "trySend",
            "HostDownlink",
            "VehicleCommandMapper",
            "CanTxGateway",
            "RemoteControlOrchestrator",
            "AuthorityManager",
        ]
        hits = _contains_any(probe_text, forbidden_patterns)
        if hits:
            errors.append(f"src/{PROBE_SOURCE}: forbidden pattern found: {', '.join(hits)}")

    normalizer_header = _read(csm_root / "include" / "board" / "remote" / "RcNormalizer.h")
    normalizer_source = _read(csm_root / "src" / "board" / "remote" / "RcNormalizer.cpp")
    if "required_channel_mask" not in normalizer_header:
        errors.append("RcNormalizerConfig missing profile required-channel mask")
    if "config_.required_channel_mask & (1u << i)" not in normalizer_source:
        errors.append("RcNormalizer must range-check only required profile channels")

    if errors:
        print("Remote Phase 2B guard failed:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("Remote Phase 2B guard passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
