#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>

#include "BoardPins.h"

#ifndef MCP_INT_DIAG_ATTACH_INTERRUPT
#define MCP_INT_DIAG_ATTACH_INTERRUPT 0
#endif

static constexpr uint32_t kSpiHz = 250000;
static constexpr uint32_t kReportPeriodMs = 250;
static constexpr uint8_t kMaxDrainPerLoop = 64;

static MCP2515 mcp2515(BoardPins::CanSpiCsN, kSpiHz);
static volatile bool irq_pending = false;
static volatile uint32_t irq_edges = 0;

static uint32_t rx_total = 0;
static uint32_t rx_err_total = 0;
static uint32_t rx_nomsg_total = 0;
static uint32_t last_report_ms = 0;
static uint32_t last_rx_id = 0;
static uint8_t last_rx_dlc = 0;
static uint8_t last_rx_data[8] = {};
static bool can_ok = false;

static void on_mcp_int_falling() {
  irq_edges++;
  irq_pending = true;
}

static void print_hex8(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

static void print_frame_data() {
  for (uint8_t i = 0; i < 8; ++i) {
    if (i != 0) {
      Serial.print(' ');
    }
    print_hex8(last_rx_data[i]);
  }
}

static bool init_mcp2515() {
  pinMode(BoardPins::CanSpiCsN, OUTPUT);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  pinMode(BoardPins::CanIntN, INPUT_PULLUP);

  SPI.begin();

  MCP2515::ERROR err = mcp2515.reset();
  Serial.print("mcp_reset=");
  Serial.println(static_cast<int>(err));
  if (err != MCP2515::ERROR_OK) {
    return false;
  }

  err = mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  Serial.print("mcp_set_bitrate=");
  Serial.println(static_cast<int>(err));
  if (err != MCP2515::ERROR_OK) {
    return false;
  }

  err = mcp2515.setNormalMode();
  Serial.print("mcp_set_normal=");
  Serial.println(static_cast<int>(err));
  if (err != MCP2515::ERROR_OK) {
    return false;
  }

  mcp2515.clearRXnOVR();
  return true;
}

static uint8_t drain_mcp_rx() {
  if (!can_ok) {
    return 0;
  }

  uint8_t drained = 0;
  while (drained < kMaxDrainPerLoop) {
    struct can_frame msg;
    const MCP2515::ERROR err = mcp2515.readMessage(&msg);
    if (err == MCP2515::ERROR_NOMSG) {
      rx_nomsg_total++;
      break;
    }
    if (err != MCP2515::ERROR_OK) {
      rx_err_total++;
      break;
    }

    const bool is_ext = (msg.can_id & CAN_EFF_FLAG) != 0;
    last_rx_id = is_ext ? (msg.can_id & CAN_EFF_MASK) : (msg.can_id & CAN_SFF_MASK);
    last_rx_dlc = msg.can_dlc > 8 ? 8 : msg.can_dlc;
    memset(last_rx_data, 0, sizeof(last_rx_data));
    memcpy(last_rx_data, msg.data, last_rx_dlc);
    rx_total++;
    drained++;
  }

  if (mcp2515.getErrorFlags() != 0) {
    mcp2515.clearRXnOVR();
  }
  return drained;
}

static void report_status(uint8_t drained) {
  noInterrupts();
  const uint32_t edges = irq_edges;
  const bool pending = irq_pending;
  interrupts();

  const int int_level = digitalRead(BoardPins::CanIntN);
  const uint8_t canintf = can_ok ? mcp2515.getInterrupts() : 0xFF;
  const uint8_t eflg = can_ok ? mcp2515.getErrorFlags() : 0xFF;
  const uint8_t canctrl = can_ok ? mcp2515.getControlRegister() : 0xFF;

  Serial.print("MCP_INT_DIAG ms=");
  Serial.print(millis());
  Serial.print(" attach=");
  Serial.print(MCP_INT_DIAG_ATTACH_INTERRUPT);
  Serial.print(" int_level=");
  Serial.print(int_level);
  Serial.print(" irq_pending=");
  Serial.print(pending ? 1 : 0);
  Serial.print(" irq_edges=");
  Serial.print(edges);
  Serial.print(" drained=");
  Serial.print(drained);
  Serial.print(" rx_total=");
  Serial.print(rx_total);
  Serial.print(" rx_err=");
  Serial.print(rx_err_total);
  Serial.print(" rx_nomsg=");
  Serial.print(rx_nomsg_total);
  Serial.print(" canintf=0x");
  print_hex8(canintf);
  Serial.print(" eflg=0x");
  print_hex8(eflg);
  Serial.print(" canctrl=0x");
  print_hex8(canctrl);
  Serial.print(" last_id=0x");
  Serial.print(last_rx_id, HEX);
  Serial.print(" dlc=");
  Serial.print(last_rx_dlc);
  Serial.print(" data=");
  print_frame_data();
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  const uint32_t start_ms = millis();
  while (!Serial && (millis() - start_ms) < 1500) {}

  Serial.println("MCP_INT_DIAG boot");
  Serial.print("can_cs_pin=");
  Serial.println(static_cast<int>(BoardPins::CanSpiCsN));
  Serial.print("can_int_pin=");
  Serial.println(static_cast<int>(BoardPins::CanIntN));
  Serial.print("digitalPinToInterrupt=");
  Serial.println(digitalPinToInterrupt(BoardPins::CanIntN));

  can_ok = init_mcp2515();
  Serial.print("can_ok=");
  Serial.println(can_ok ? 1 : 0);

#if MCP_INT_DIAG_ATTACH_INTERRUPT
  if (can_ok) {
    attachInterrupt(digitalPinToInterrupt(BoardPins::CanIntN), on_mcp_int_falling, FALLING);
    irq_pending = !digitalRead(BoardPins::CanIntN);
    Serial.println("attach_interrupt=1");
  }
#else
  Serial.println("attach_interrupt=0");
#endif

  last_report_ms = millis();
}

void loop() {
  bool should_drain = digitalRead(BoardPins::CanIntN) == LOW;

#if MCP_INT_DIAG_ATTACH_INTERRUPT
  noInterrupts();
  const bool pending = irq_pending;
  irq_pending = false;
  interrupts();
  should_drain = should_drain || pending;
#endif

  uint8_t drained = 0;
  if (should_drain) {
    drained = drain_mcp_rx();
  }

  const uint32_t now_ms = millis();
  if (now_ms - last_report_ms >= kReportPeriodMs) {
    report_status(drained);
    last_report_ms = now_ms;
  }
}
