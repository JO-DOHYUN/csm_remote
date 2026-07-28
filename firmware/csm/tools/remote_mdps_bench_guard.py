import configparser
from pathlib import Path


Import("env")

BENCH_ENV = "env:portenta_h7_m7_mid_mcp2515_j4_remote_product_mdps_bench_wifi"
BASE_ENV = "env:portenta_h7_m7_mid_mcp2515_j4_remote_product_wifi"
PRODUCT_ENV = "env:portenta_h7_m7_mid_feeder_uart_j4_remote_product_wifi"


def fail(message):
    print(f"Remote MDPS bench guard failed: {message}")
    env.Exit(1)


root = Path(env.subst("$PROJECT_DIR"))
parser = configparser.RawConfigParser()
parser.optionxform = str
parser.read(root / "platformio.ini", encoding="utf-8")

if not parser.has_section(BENCH_ENV):
    fail(f"missing {BENCH_ENV}")
if parser.get(BENCH_ENV, "extends", fallback="") != BASE_ENV:
    fail(f"{BENCH_ENV} must extend {BASE_ENV}")

flags = parser.get(BENCH_ENV, "build_flags", fallback="")
required = (
    "BOARD_CSM_PROFILE_REMOTE_MDPS_BENCH=1",
    "BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING=0",
    "BOARD_ENABLE_MDPS_BENCH_MAPPING=1",
    "BOARD_ENABLE_MCP2515=1",
    "BOARD_ENABLE_MCP2515_INIT=1",
    "BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT=0",
)
for item in required:
    if item not in flags:
        fail(f"missing {item}")

product_flags = parser.get(PRODUCT_ENV, "build_flags", fallback="")
for item in (
    "BOARD_CSM_PROFILE_REMOTE_PRODUCT=1",
    "BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING=1",
    "BOARD_ENABLE_MDPS_BENCH_MAPPING=0",
    "BOARD_HW_PROFILE_MID_FEEDER_UART=1",
    "BOARD_ENABLE_FEEDER_UART=1",
    "BOARD_ENABLE_HOST_DOWNLINK=0",
    "BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED=1",
):
    if item not in product_flags:
        fail(f"product RC contract missing {item}")

base_flags = parser.get(BASE_ENV, "build_flags", fallback="")
for item in (
    "BOARD_CSM_PROFILE_REMOTE_PRODUCT=1",
    "BOARD_ENABLE_REMOTE_CONTROL=1",
    "BOARD_ENABLE_REMOTE_AUTHORITY=1",
    "BOARD_ENABLE_HOST_CAN_TX=0",
    "BOARD_ENABLE_HOST_CAN_TX_BUILTIN=0",
    "BOARD_ENABLE_HOST_CAN_TX_MCP2515=0",
    "BOARD_ENABLE_HOST_DOWNLINK=0",
    "BOARD_MCP2515_CONTROL_TX_ALLOWED=0",
    "BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED=1",
):
    if item not in base_flags:
        fail(f"base product contract missing {item}")

main_source = (root / "src" / "main.cpp").read_text(encoding="utf-8")
for item in (
    "#if BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING",
    "#if BOARD_CSM_PROFILE_REMOTE_MDPS_BENCH",
    "inputs.local_tx_inhibit_latched = false;",
    "AutonomyAuthorityState::InactiveConfirmed;",
    "inputs.local_tx_inhibit_latched = true;",
    "AutonomyAuthorityState::Unknown;",
):
    if item not in main_source:
        fail(f"main wiring missing {item}")

print("Remote MDPS bench profile guard passed.")
