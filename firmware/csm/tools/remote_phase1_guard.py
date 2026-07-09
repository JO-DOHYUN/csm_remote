from pathlib import Path
import sys


def _read(path):
    return path.read_text(encoding="utf-8", errors="ignore")


def _find_hits(root, files, patterns):
    hits = []
    for rel in files:
        path = root / rel
        if not path.exists():
            hits.append(f"{rel}: missing file")
            continue
        text = _read(path)
        for pattern in patterns:
            if pattern in text:
                hits.append(f"{rel}: forbidden pattern {pattern!r}")
    return hits


def _contains_any(text, patterns):
    return [pattern for pattern in patterns if pattern in text]


def main():
    csm_root = Path(__file__).resolve().parents[1]
    errors = []

    phase1_files = [
        Path("include/board/authority/AuthorityTypes.h"),
        Path("include/board/authority/AutonomyAuthorityMonitor.h"),
        Path("include/board/authority/AuthorityManager.h"),
        Path("src/board/authority/AutonomyAuthorityMonitor.cpp"),
        Path("src/board/authority/AuthorityManager.cpp"),
        Path("include/board/control/OperatorCommand.h"),
        Path("include/board/control/CommandLimiter.h"),
        Path("include/board/control/VehicleCommandMapper.h"),
        Path("include/board/control/CanTxGateway.h"),
        Path("include/board/control/RemoteControlOrchestrator.h"),
        Path("src/board/control/CommandLimiter.cpp"),
        Path("src/board/control/VehicleCommandMapper.cpp"),
        Path("src/board/control/CanTxGateway.cpp"),
        Path("src/board/control/RemoteControlOrchestrator.cpp"),
        Path("include/board/remote/RemoteTypes.h"),
        Path("include/board/remote/CrsfParser.h"),
        Path("include/board/remote/RcNormalizer.h"),
        Path("include/board/remote/M4RemoteMailboxContract.h"),
        Path("include/board/remote/M4RemoteMailboxWriter.h"),
        Path("include/board/remote/M4RemoteMailboxReader.h"),
        Path("include/board/remote/RemoteContractSelfTest.h"),
        Path("include/board/remote/RemoteControlSource.h"),
        Path("src/board/remote/CrsfParser.cpp"),
        Path("src/board/remote/RcNormalizer.cpp"),
        Path("src/board/remote/M4RemoteMailboxContract.cpp"),
        Path("src/board/remote/M4RemoteMailboxWriter.cpp"),
        Path("src/board/remote/M4RemoteMailboxReader.cpp"),
        Path("src/board/remote/RemoteContractSelfTest.cpp"),
        Path("src/board/remote/RemoteControlSource.cpp"),
    ]

    remote_files = [
        rel for rel in phase1_files
        if str(rel).startswith("include/board/remote") or str(rel).startswith("src/board/remote")
    ]
    active_io_patterns = [
        "submitTx",
        "CAN::write",
        "sendMessage",
        "trySend",
        "CanTxEnable",
        "HostDownlink",
        "Serial3",
        "HardwareSerial",
        "digitalWrite",
        "HSEM",
        "OpenAMP",
        "RPC",
    ]
    errors.extend(_find_hits(csm_root, phase1_files, active_io_patterns))

    remote_can_patterns = [
        "can_id",
        "CanFrameRequest",
        "CanTxGateway",
        "CANExtended",
        "CANStandard",
    ]
    errors.extend(_find_hits(csm_root, remote_files, remote_can_patterns))

    main_cpp = csm_root / "src/main.cpp"
    main_hits = _contains_any(
        _read(main_cpp),
        [
            "RemoteControlOrchestrator",
            "CrsfParser",
            "RcNormalizer",
            "M4RemoteMailbox",
            "RemoteControlSource",
        ],
    )
    if main_hits:
        errors.append(f"src/main.cpp wires Phase 1 remote skeleton: {', '.join(main_hits)}")

    platformio = _read(csm_root / "platformio.ini")
    platformio_hits = _contains_any(
        platformio,
        [
            "portenta_h7_m4",
            "BOARD_ENABLE_REMOTE_CONTROL=1",
            "BOARD_ENABLE_M4_REMOTE_FRONTEND=1",
            "BOARD_ENABLE_REMOTE_AUTHORITY=1",
        ],
    )
    if platformio_hits:
        errors.append(
            "platformio.ini enables Phase 2 remote runtime before Phase 1 closure: "
            + ", ".join(platformio_hits)
        )

    mapper_cpp = _read(csm_root / "src/board/control/VehicleCommandMapper.cpp")
    if "kDetailNoVehicleMapping" not in mapper_cpp:
        errors.append("VehicleCommandMapper.cpp no longer contains the no-real-mapping reject detail")
    if "result.mapped = true" in mapper_cpp:
        errors.append("VehicleCommandMapper.cpp maps real frames during Phase 1")

    gateway_cpp = _read(csm_root / "src/board/control/CanTxGateway.cpp")
    gateway_forbidden = _contains_any(gateway_cpp, ["submitTx", "CAN::write", "digitalWrite"])
    if gateway_forbidden:
        errors.append("CanTxGateway.cpp performs active IO: " + ", ".join(gateway_forbidden))

    if errors:
        print("Remote Phase 1 guard failed:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("Remote Phase 1 guard passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
