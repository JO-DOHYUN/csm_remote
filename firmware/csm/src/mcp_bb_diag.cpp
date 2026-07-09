#include <Arduino.h>

#include "BoardPins.h"

static uint8_t bb_transfer(uint8_t out) {
  uint8_t in = 0;
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(BoardPins::SpiSck, LOW);
    digitalWrite(BoardPins::SpiCopi, (out & (1u << bit)) ? HIGH : LOW);
    delayMicroseconds(50);
    digitalWrite(BoardPins::SpiSck, HIGH);
    delayMicroseconds(50);
    in = static_cast<uint8_t>((in << 1) | (digitalRead(BoardPins::SpiCipo) ? 1 : 0));
  }
  digitalWrite(BoardPins::SpiSck, LOW);
  delayMicroseconds(50);
  return in;
}

static void mcp_reset_bb() {
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  digitalWrite(BoardPins::SpiSck, LOW);
  digitalWrite(BoardPins::SpiCopi, LOW);
  delayMicroseconds(100);
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  delayMicroseconds(100);
  bb_transfer(0xC0);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  delay(20);
}

static uint8_t mcp_read_bb(uint8_t reg) {
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  delayMicroseconds(100);
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  delayMicroseconds(100);
  bb_transfer(0x03);
  bb_transfer(reg);
  const uint8_t value = bb_transfer(0x00);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  return value;
}

static void print_hex8(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void setup() {
  Serial.begin(115200);
  const uint32_t start_ms = millis();
  while (!Serial && millis() - start_ms < 1500) {}

  pinMode(BoardPins::CanSpiCsN, OUTPUT);
  pinMode(BoardPins::SpiCopi, OUTPUT);
  pinMode(BoardPins::SpiSck, OUTPUT);
  pinMode(BoardPins::SpiCipo, INPUT);
  pinMode(BoardPins::CanIntN, INPUT_PULLUP);

  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  digitalWrite(BoardPins::SpiCopi, LOW);
  digitalWrite(BoardPins::SpiSck, LOW);

  Serial.println("MCP_BB_DIAG_BOOT");
  Serial.flush();
}

void loop() {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if (now - last_ms < 1000) {
    return;
  }
  last_ms = now;

  const uint8_t idle_miso = digitalRead(BoardPins::SpiCipo) ? 1 : 0;
  const uint8_t int_level = digitalRead(BoardPins::CanIntN) ? 1 : 0;

  mcp_reset_bb();
  const uint8_t canstat = mcp_read_bb(0x0E);
  const uint8_t canctrl = mcp_read_bb(0x0F);
  const uint8_t canintf = mcp_read_bb(0x2C);
  const uint8_t eflg = mcp_read_bb(0x2D);

  Serial.print("MCP_BB_DIAG idle_miso=");
  Serial.print(idle_miso);
  Serial.print(" int=");
  Serial.print(int_level);
  Serial.print(" regs=0x");
  print_hex8(canstat);
  Serial.print(",0x");
  print_hex8(canctrl);
  Serial.print(",0x");
  print_hex8(canintf);
  Serial.print(",0x");
  print_hex8(eflg);
  Serial.println();
  Serial.flush();
}
