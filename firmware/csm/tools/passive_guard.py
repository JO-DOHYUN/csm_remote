from pathlib import Path
import configparser
import subprocess

from SCons.Script import Exit, Import

Import("env")


def _define_value(cppdefines, name, default="0"):
    for item in cppdefines:
        if isinstance(item, tuple) and item and item[0] == name:
            return str(item[1])
        if isinstance(item, str):
            if item == name:
                return "1"
            prefix = f"{name}="
            if item.startswith(prefix):
                return item[len(prefix):]
    return default


def _truthy(value):
    return value not in ("", "0", "false", "False", "FALSE")


def _platformio_defines(project_dir, env_name):
    parser = configparser.RawConfigParser()
    parser.optionxform = str
    ini_path = Path(project_dir) / "platformio.ini"
    if not ini_path.exists():
        return []
    parser.read(ini_path, encoding="utf-8")
    result = []
    seen = set()

    def visit(section):
        if section in seen or not parser.has_section(section):
            return
        seen.add(section)
        extends = parser.get(section, "extends", fallback="")
        for parent in extends.splitlines():
            parent = parent.strip()
            if parent:
                visit(parent)
        flags = parser.get(section, "build_flags", fallback="")
        for line in flags.splitlines():
            line = line.strip()
            if not line.startswith("-D"):
                continue
            define = line[2:].strip()
            if not define or define.startswith("$"):
                continue
            if "=" in define:
                name, value = define.split("=", 1)
                result.append((name.strip(), value.strip()))
            else:
                result.append(define.strip())

    visit(f"env:{env_name}")
    return result


cppdefines = list(env.get("CPPDEFINES", []))
parsed_flags = env.ParseFlags(env.GetProjectOption("build_flags"))
cppdefines.extend(parsed_flags.get("CPPDEFINES", []))
expanded_flags = env.ParseFlags(env.subst("$BUILD_FLAGS"))
cppdefines.extend(expanded_flags.get("CPPDEFINES", []))
env_name = env.subst("$PIOENV")
cppdefines.extend(_platformio_defines(env.subst("$PROJECT_DIR"), env_name))
is_passive = env_name.endswith("_passive") or _truthy(
    _define_value(cppdefines, "BOARD_CSM_PROFILE_PASSIVE_PRODUCT")
)

if is_passive:
    required_false = [
        "BOARD_CSM_PROFILE_FULL_INSTRUMENTED",
        "BOARD_ENABLE_HOST_CAN_TX",
        "BOARD_ENABLE_HOST_CAN_TX_BUILTIN",
        "BOARD_ENABLE_HOST_CAN_TX_MCP2515",
        "BOARD_ENABLE_HOST_DOWNLINK",
        "BOARD_ENABLE_BUILTIN_CAN_TX_TEST",
        "BOARD_ENABLE_MCP2515_TX_TEST",
        "BOARD_ENABLE_CAN_TX_GATE_FOR_TEST",
        "BOARD_MCP2515_CONTROL_TX_ALLOWED",
        "BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED",
        "BOARD_USB_CDC_RECONNECT_RESET_MS",
    ]
    required_true = [
        ("BOARD_CSM_PROFILE_PASSIVE_PRODUCT", "0"),
        ("BOARD_ENABLE_MCP2515", "1"),
        ("BOARD_ENABLE_BUILTIN_CAN_RX", "0"),
        ("BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT", "0"),
        ("BOARD_CAN_TRANSCEIVER_ENABLE_FOR_RX", "0"),
        ("BOARD_PRODUCT_ACK_OBSERVE_MODE", "0"),
        ("BOARD_PRODUCT_PRE_SESSION_SAFE_RECEIVE", "0"),
        ("BOARD_PASSIVE_DEFER_CAN_FRONTEND_INIT_UNTIL_SESSION", "0"),
        ("BOARD_USB_CDC_DTR_SESSION_REQUIRED", "1"),
        ("BOARD_USB_CDC_DTR_SESSION_ONLY", "1"),
    ]
    config_errors = []
    for name in required_false:
        if _truthy(_define_value(cppdefines, name)):
            config_errors.append(f"{name} must be 0 in passive product builds")
    for name, default in required_true:
        if not _truthy(_define_value(cppdefines, name, default)):
            config_errors.append(f"{name} must be 1 in 2-bus passive product builds")
    if config_errors:
        print("Passive guard failed: invalid passive build flags:")
        for error in config_errors:
            print(f"  - {error}")
        Exit(1)

    def _check_passive_symbols(target, source, env):
        elf = Path(str(target[0]))
        nm = env.subst("$NM")
        if not nm or nm == "$NM":
            nm = "arm-none-eabi-nm"
        if not elf.exists():
            print(f"Passive guard failed: ELF not found: {elf}")
            Exit(1)
        try:
            output = subprocess.check_output(
                f'"{nm}" -C "{elf}"',
                shell=True,
                text=True,
                errors="ignore",
            )
        except Exception as exc:
            print(f"Passive guard failed: unable to inspect symbols: {exc}")
            Exit(1)

        forbidden = [
            "HostDownlinkParser",
            "handle_host_can_tx_request",
            "handle_host_heartbeat",
            "handle_host_control_session",
            "handle_host_clear_fault_lockout",
            "start_mcp2515_tx_audit",
            "service_mcp2515_tx_test",
            "service_builtin_can_tx_test",
            "NVIC_SystemReset",
            "CAN::write",
        ]
        hits = [symbol for symbol in forbidden if symbol in output]
        if hits:
            print("Passive guard failed: forbidden active symbols linked:")
            for symbol in hits:
                print(f"  - {symbol}")
            Exit(1)

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _check_passive_symbols)
