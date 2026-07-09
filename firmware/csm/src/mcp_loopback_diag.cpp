#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>

#include "BoardPins.h"

static constexpr uint32_t kSpiHz = 250000;
static MCP2515 mcp2515(BoardPins::CanSpiCsN, kSpiHz);
static uint32_t tx_count = 0;
static uint32_t rx_count = 0;
static bool can_ok = false;

static void print_hex8(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

static bool init_mcp() {
  pinMode(BoardPins::CanSpiCsN, OUTPUT);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  pinMode(BoardPins::CanIntN, INPUT_PULLUP);
  SPI.begin();

  MCP2515::ERROR err = mcp2515.reset();
  Serial.print("reset=");
  Serial.println(static_cast<int>(err));
  if (err != MCP2515::ERROR_OK) {
    return false;
  }

  err = mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  Serial.print("bitrate=");
  Serial.println(static_cast<int>(err));
  if (err != MCP2515::ERROR_OK) {
    return false;
  }

  err = mcp2515.setLoopbackMode();
  Serial.print("loopback=");
  Serial.println(static_cast<int>(err));
  return err == MCP2515::ERROR_OK;
}

void setup() {
  Serial.begin(115200);
  const uint32_t start_ms = millis();
  while (!Serial && millis() - start_ms < 1500) {}
  Serial.println("MCP_LOOPBACK_DIAG_BOOT");
  can_ok = init_mcp();
  Serial.print("can_ok=");
  Serial.println(can_ok ? 1 : 0);
  Serial.flush();
}

void loop() {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if (now - last_ms < 1000) {
    return;
  }
  last_ms = now;

  struct can_frame tx = {};
  tx.can_id = 0x321;
  tx.can_dlc = 8;
  tx.data[0] = 0xC5;
  tx.data[1] = 0x11;
  tx.data[2] = static_cast<uint8_t>(tx_count & 0xFF);
  tx.data[3] = static_cast<uint8_t>((tx_count >> 8) & 0xFF);
  tx.data[4] = static_cast<uint8_t>((tx_count >> 16) & 0xFF);
  tx.data[5] = static_cast<uint8_t>((tx_count >> 24) & 0xFF);
  tx.data[6] = 0x08;
  tx.data[7] = 0x00;

  MCP2515::ERROR tx_err = MCP2515::ERROR_FAIL;
  if (can_ok) {
    tx_err = mcp2515.sendMessage(&tx);
    if (tx_err == MCP2515::ERROR_OK) {
      tx_count++;
    }
  }

  uint8_t drained = 0;
  struct can_frame rx = {};
  MCP2515::ERROR rx_err = MCP2515::ERROR_NOMSG;
  if (can_ok) {
    for (; drained < 8; ++drained) {
      rx_err = mcp2515.readMessage(&rx);
      if (rx_err != MCP2515::ERROR_OK) {
        break;
      }
      rx_count++;
    }
  }

  const uint8_t canintf = can_ok ? mcp2515.getInterrupts() : 0xFF;
  const uint8_t eflg = can_ok ? mcp2515.getErrorFlags() : 0xFF;
  const uint8_t canctrl = can_ok ? mcp2515.getControlRegister() : 0xFF;

  Serial.print("MCP_LOOPBACK tx_err=");
  Serial.print(static_cast<int>(tx_err));
  Serial.print(" rx_err=");
  Serial.print(static_cast<int>(rx_err));
  Serial.print(" drained=");
  Serial.print(drained);
  Serial.print(" tx=");
  Serial.print(tx_count);
  Serial.print(" rx=");
  Serial.print(rx_count);
  Serial.print(" canintf=0x");
  print_hex8(canintf);
  Serial.print(" eflg=0x");
  print_hex8(eflg);
  Serial.print(" canctrl=0x");
  print_hex8(canctrl);
  Serial.print(" last_id=0x");
  Serial.print(rx.can_id & CAN_SFF_MASK, HEX);
  Serial.print(" dlc=");
  Serial.print(rx.can_dlc);
  Serial.print(" data=");
  for (uint8_t i = 0; i < 8; ++i) {
    if (i) {
      Serial.print(' ');
    }
    print_hex8(rx.data[i]);
  }
  Serial.println();
  Serial.flush();
}
