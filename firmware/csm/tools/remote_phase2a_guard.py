from pathlib import Path
import configparser
import sys


M4_ENV = "env:portenta_h7_m4_remote_frontend_build_proof"


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
            "+<m4_remote_frontend_build_proof.cpp>",
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
            "board/control",
            "board/authority",
            "HostDownlink",
            "CanTxGateway",
            "VehicleCommandMapper",
        ]
        for forbidden in forbidden_sources:
            if forbidden in src_filter:
                errors.append(f"{M4_ENV} includes forbidden source {forbidden}")

        flags = "\n".join(_section_lines(parser, M4_ENV, "build_flags"))
        required_flags = [
            "-D BOARD_M4_REMOTE_FRONTEND_BUILD_PROOF=1",
            "-D BOARD_ENABLE_REMOTE_CONTROL=0",
            "-D BOARD_ENABLE_M4_REMOTE_FRONTEND=0",
            "-D BOARD_ENABLE_REMOTE_AUTHORITY=0",
            "-D BOARD_ENABLE_HOST_CAN_TX=0",
            "-D BOARD_ENABLE_HOST_DOWNLINK=0",
        ]
        for required in required_flags:
            if required not in flags:
                errors.append(f"{M4_ENV} missing build flag {required}")

    m7_sections = [
        "env:portenta_h7_m7",
        "env:portenta_h7_m7_mid_dual_can20",
    ]
    for section in m7_sections:
        if not parser.has_section(section):
            errors.append(f"platformio.ini missing {section}")
            continue
        src_filter = "\n".join(_section_lines(parser, section, "build_src_filter"))
        if "-<m4_remote_frontend_build_proof.cpp>" not in src_filter:
            errors.append(f"{section} must exclude m4_remote_frontend_build_proof.cpp")

    scanned_files = [
        Path("src/m4_remote_frontend_build_proof.cpp"),
        Path("src/board/remote/RemoteTypes.cpp"),
        Path("src/board/remote/CrsfParser.cpp"),
        Path("src/board/remote/RcNormalizer.cpp"),
        Path("src/board/remote/M4RemoteMailboxContract.cpp"),
        Path("src/board/remote/M4RemoteMailboxWriter.cpp"),
    ]
    active_io_patterns = [
        "Serial3",
        "HardwareSerial",
        "digitalWrite",
        "HSEM",
        "OpenAMP",
        "RPC",
        "CAN::write",
        "submitTx",
        "sendMessage",
        "trySend",
    ]
    for rel in scanned_files:
        path = csm_root / rel
        if not path.exists():
            errors.append(f"{rel}: missing file")
            continue
        hits = _contains_any(_read(path), active_io_patterns)
        if hits:
            errors.append(f"{rel}: active IO pattern found: {', '.join(hits)}")

    if errors:
        print("Remote Phase 2A guard failed:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("Remote Phase 2A guard passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
