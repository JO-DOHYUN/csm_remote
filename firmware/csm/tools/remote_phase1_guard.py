import configparser
from pathlib import Path
import sys


M4_ENV = "env:portenta_h7_m4_remote_frontend"
M7_ENV = "env:portenta_h7_m7_mid_feeder_uart_j4_remote_product_wifi"
M7_BASE_ENV = "env:portenta_h7_m7_mid_mcp2515_j4_remote_product_wifi"


def read(path):
    return path.read_text(encoding="utf-8", errors="strict")


def lines(parser, section, option):
    value = parser.get(section, option, fallback="")
    return [line.strip() for line in value.splitlines() if line.strip()]


def require_patterns(errors, label, text, patterns):
    for pattern in patterns:
        if pattern not in text:
            errors.append(f"{label}: missing {pattern!r}")


def forbid_patterns(errors, label, text, patterns):
    for pattern in patterns:
        if pattern in text:
            errors.append(f"{label}: forbidden {pattern!r}")


def main():
    root = Path(__file__).resolve().parents[1]
    errors = []
    parser = configparser.RawConfigParser()
    parser.optionxform = str
    parser.read(root / "platformio.ini", encoding="utf-8")

    required_files = [
        "src/m4_remote_frontend.cpp",
        "include/board/remote/RemoteSharedMemory.h",
        "src/board/remote/RemoteSharedMemory.cpp",
        "include/board/control/RemoteControlRuntime.h",
        "src/board/control/RemoteControlRuntime.cpp",
        "test/remote_control_contract_test.cpp",
        "pc_tools/remote_control_hil.py",
        "pc_tools/kvaser_remote_control_hil.py",
    ]
    for rel in required_files:
        if not (root / rel).is_file():
            errors.append(f"missing required file: {rel}")

    if not parser.has_section(M4_ENV):
        errors.append(f"missing {M4_ENV}")
    else:
        m4_sources = "\n".join(lines(parser, M4_ENV, "build_src_filter"))
        m4_flags = "\n".join(lines(parser, M4_ENV, "build_flags"))
        m4_ignored = "\n".join(lines(parser, M4_ENV, "lib_ignore"))
        if parser.get(M4_ENV, "board", fallback="") != "portenta_h7_m4":
            errors.append(f"{M4_ENV}: board must be portenta_h7_m4")
        require_patterns(errors, M4_ENV, m4_sources, [
            "+<m4_remote_frontend.cpp>",
            "+<board/remote/RemoteSharedMemory.cpp>",
        ])
        forbid_patterns(errors, M4_ENV, m4_sources, [
            "+<main.cpp>", "+<board/control", "+<board/authority",
        ])
        require_patterns(errors, M4_ENV, m4_flags, [
            "BOARD_ENABLE_M4_REMOTE_FRONTEND=1",
            "BOARD_ENABLE_REMOTE_AUTHORITY=0",
            "BOARD_ENABLE_HOST_CAN_TX=0",
            "BOARD_ENABLE_HOST_DOWNLINK=0",
            "BOARD_M4_REMOTE_BAUD=416666UL",
            "BOARD_M4_REMOTE_STALE_MS=100UL",
            "BOARD_M4_REMOTE_PUBLISH_MS=20UL",
        ])
        require_patterns(errors, M4_ENV, m4_ignored, ["RPC", "openamp_arduino"])

    if not parser.has_section(M7_ENV):
        errors.append(f"missing {M7_ENV}")
    else:
        m7_sources = "\n".join(
            lines(parser, M7_BASE_ENV, "build_src_filter")
            + lines(parser, M7_ENV, "build_src_filter")
        )
        m7_flags = "\n".join(lines(parser, M7_ENV, "build_flags"))
        m7_ignored = "\n".join(lines(parser, M7_BASE_ENV, "lib_ignore"))
        require_patterns(errors, M7_ENV, m7_sources, [
            "+<board/remote/RemoteSharedMemory.cpp>",
            "+<board/uplink/WifiTcpSink.cpp>",
            "+<board/feeder/FeederUartIngress.cpp>",
        ])
        require_patterns(errors, M7_ENV, m7_flags, [
            "BOARD_CSM_PROFILE_REMOTE_PRODUCT=1",
            "BOARD_ENABLE_REMOTE_CONTROL=1",
            "BOARD_ENABLE_REMOTE_AUTHORITY=1",
            "BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING=1",
            "BOARD_ENABLE_MDPS_BENCH_MAPPING=0",
            "BOARD_HW_PROFILE_MID_FEEDER_UART=1",
            "BOARD_ENABLE_FEEDER_UART=1",
            "BOARD_ENABLE_MCP2515=0",
            "BOARD_ENABLE_MCP2515_INIT=0",
            "BOARD_ENABLE_HOST_CAN_TX=0",
            "BOARD_ENABLE_HOST_CAN_TX_BUILTIN=0",
            "BOARD_ENABLE_HOST_CAN_TX_MCP2515=0",
            "BOARD_ENABLE_HOST_DOWNLINK=0",
            "BOARD_MCP2515_CONTROL_TX_ALLOWED=0",
            "BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT=1",
            "BOARD_BUILTIN_CAN_BUS_ID=1",
            "BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED=1",
            "BOARD_ENABLE_BUILTIN_CAN_TX_TEST=0",
            "BOARD_ENABLE_MCP2515_TX_TEST=0",
            "BOARD_ENABLE_PERIODIC_CAPABILITY=0",
        ])
        require_patterns(errors, M7_ENV, m7_ignored, ["RPC", "openamp_arduino"])

    base_sources = "\n".join(lines(parser, "env:portenta_h7_m7", "build_src_filter"))
    require_patterns(errors, "env:portenta_h7_m7", base_sources, [
        "-<m4_remote_frontend.cpp>",
        "-<board/remote/RemoteSharedMemory.cpp>",
    ])

    m4_source = read(root / "src/m4_remote_frontend.cpp")
    require_patterns(errors, "m4_remote_frontend.cpp", m4_source, [
        "Serial3.begin", "Serial3.available", "Serial3.write",
        "decodeCrsfRcChannelsPacked", "decodeCrsfLinkStatistics",
        "publishRemoteSharedSample", "readRemoteTelemetry",
    ])
    forbid_patterns(errors, "m4_remote_frontend.cpp", m4_source, [
        "CanTxGateway", "VehicleCommandMapper", "AuthorityManager",
        "CANMessage", "builtin_can", "0x503", "0x510",
    ])

    shared = read(root / "include/board/remote/RemoteSharedMemory.h")
    require_patterns(errors, "RemoteSharedMemory.h", shared, [
        "kRemoteSharedMemoryAddress = 0x38000000u",
        "kRemoteSharedMemoryReservedBytes = 0x0400u",
        "kRemoteSharedMemoryVersion = 2",
        "struct alignas(32) RemoteM4ToM7Channel",
        "struct alignas(32) RemoteM7ToM4Channel",
        "offsetof(RemoteSharedMemoryRegion, m4_to_m7) % 32u",
        "offsetof(RemoteSharedMemoryRegion, m7_to_m4) % 32u",
        "static_assert(sizeof(RemoteSharedMemoryRegion)",
    ])
    shared_cpp = read(root / "src/board/remote/RemoteSharedMemory.cpp")
    require_patterns(errors, "RemoteSharedMemory.cpp", shared_cpp, [
        "invalidateM7Cache(&channel, sizeof(channel))",
        "cleanM7Cache(&channel, sizeof(channel))",
    ])

    authority = read(root / "src/board/authority/AuthorityManager.cpp")
    remote_index = authority.find("inputs.remote_source_present")
    autonomy_index = authority.find("switch (inputs.autonomy_state)")
    if remote_index < 0 or autonomy_index < 0 or autonomy_index >= remote_index:
        errors.append("AuthorityManager: autonomy arbitration must precede RC reservation")

    mapper = read(root / "src/board/control/VehicleCommandMapper.cpp")
    require_patterns(errors, "VehicleCommandMapper.cpp", mapper, [
        "kRemoteSteeringCanId", "kRemoteSteeringCenter",
        "kRemoteAuxiliaryNegative", "kRemoteAuxiliaryPositive",
        "steering_overlay_permille", "momentary_overlay_permille",
        "VehicleCommandMapping::Vehicle0x005And0x007",
        "kDetailNoVehicleMapping", "result.mapped = true",
    ])
    gateway = read(root / "src/board/control/CanTxGateway.cpp")
    forbid_patterns(errors, "CanTxGateway.cpp", gateway, [
        "CAN::write", "CANMessage", "digitalWrite", "Serial3",
    ])

    main_cpp = read(root / "src/main.cpp")
    require_patterns(errors, "main.cpp", main_cpp, [
        "RemoteControlRuntime", "service_remote_control", "bootM4()",
        "emit_remote_control_state", "emit_can_tx_raw",
        "required_can_lanes_ok()",
        "Remote Product keeps app/host control compiled out",
        "BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING",
        "BOARD_REMOTE_LOCAL_CAN_TX_ENABLED",
        "VehicleCommandMapping::Vehicle0x005And0x007",
        "VehicleCommandMapping::None",
    ])

    records = read(root / "include/protocol/TypedRecords.h")
    require_patterns(errors, "TypedRecords.h", records, [
        "kRemoteControlStatePayloadLen = 228",
        "kRemoteControlStateSchema = 2",
        "kRemoteControlStateLastIpcRejectDetailOffset = 111",
        "kRemoteControlStateSharedPublishFailuresOffset = 170",
        "kRemoteControlStateLastRcAgeOffset = 216",
    ])

    if errors:
        print("Remote product architecture guard failed:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print("Remote product architecture guard passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
