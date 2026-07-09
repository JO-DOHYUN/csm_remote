#include <Arduino.h>
#include <SPI.h>

#include "BoardPins.h"

static constexpr uint16_t kSlowDelayUs = 1000;

static void print_hex8(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

static uint8_t bb_transfer_mode0(uint8_t out) {
  uint8_t in = 0;
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(BoardPins::SpiSck, LOW);
    digitalWrite(BoardPins::SpiCopi, (out & (1u << bit)) ? HIGH : LOW);
    delayMicroseconds(kSlowDelayUs);
    digitalWrite(BoardPins::SpiSck, HIGH);
    delayMicroseconds(kSlowDelayUs);
    in = static_cast<uint8_t>((in << 1) | (digitalRead(BoardPins::SpiCipo) ? 1 : 0));
  }
  digitalWrite(BoardPins::SpiSck, LOW);
  delayMicroseconds(kSlowDelayUs);
  return in;
}

static uint8_t bb_read_register(uint8_t reg) {
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  digitalWrite(BoardPins::SpiSck, LOW);
  digitalWrite(BoardPins::SpiCopi, LOW);
  delayMicroseconds(kSlowDelayUs);
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  delayMicroseconds(kSlowDelayUs);
  bb_transfer_mode0(0x03);
  bb_transfer_mode0(reg);
  const uint8_t value = bb_transfer_mode0(0x00);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  delayMicroseconds(kSlowDelayUs);
  return value;
}

static uint8_t bb_read_status() {
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  digitalWrite(BoardPins::SpiSck, LOW);
  digitalWrite(BoardPins::SpiCopi, LOW);
  delayMicroseconds(kSlowDelayUs);
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  delayMicroseconds(kSlowDelayUs);
  bb_transfer_mode0(0xA0);
  const uint8_t value = bb_transfer_mode0(0x00);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  delayMicroseconds(kSlowDelayUs);
  return value;
}

static void bb_reset() {
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  digitalWrite(BoardPins::SpiSck, LOW);
  digitalWrite(BoardPins::SpiCopi, LOW);
  delayMicroseconds(kSlowDelayUs);
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  delayMicroseconds(kSlowDelayUs);
  bb_transfer_mode0(0xC0);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  delay(50);
}

static uint8_t hw_transfer_read_register(uint8_t reg, uint8_t mode) {
  SPI.beginTransaction(SPISettings(10000, MSBFIRST, mode));
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  delayMicroseconds(kSlowDelayUs);
  SPI.transfer(0x03);
  SPI.transfer(reg);
  const uint8_t value = SPI.transfer(0x00);
  delayMicroseconds(kSlowDelayUs);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  SPI.endTransaction();
  delayMicroseconds(kSlowDelayUs);
  return value;
}

static uint8_t hw_transfer_read_status(uint8_t mode) {
  SPI.beginTransaction(SPISettings(10000, MSBFIRST, mode));
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  delayMicroseconds(kSlowDelayUs);
  SPI.transfer(0xA0);
  const uint8_t value = SPI.transfer(0x00);
  delayMicroseconds(kSlowDelayUs);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  SPI.endTransaction();
  delayMicroseconds(kSlowDelayUs);
  return value;
}

static void print_regs(const char* label,
                       uint8_t canstat,
                       uint8_t canctrl,
                       uint8_t canintf,
                       uint8_t eflg,
                       uint8_t status) {
  Serial.print(label);
  Serial.print(" regs=0x");
  print_hex8(canstat);
  Serial.print(",0x");
  print_hex8(canctrl);
  Serial.print(",0x");
  print_hex8(canintf);
  Serial.print(",0x");
  print_hex8(eflg);
  Serial.print(" status=0x");
  print_hex8(status);
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

  SPI.begin();

  Serial.println("MCP_SPI_VARIANTS_DIAG_BOOT");
  Serial.flush();
}

void loop() {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if (now - last_ms < 2000) {
    return;
  }
  last_ms = now;

  const uint8_t idle_so = digitalRead(BoardPins::SpiCipo) ? 1 : 0;
  const uint8_t int_level = digitalRead(BoardPins::CanIntN) ? 1 : 0;

  bb_reset();

  Serial.print("MCP_SPI_VARIANTS idle_so=");
  Serial.print(idle_so);
  Serial.print(" int=");
  Serial.print(int_level);
  Serial.print(" | ");

  print_regs("BB_SLOW",
             bb_read_register(0x0E),
             bb_read_register(0x0F),
             bb_read_register(0x2C),
             bb_read_register(0x2D),
             bb_read_status());
  Serial.print(" | ");

  print_regs("HW_MODE0",
             hw_transfer_read_register(0x0E, SPI_MODE0),
             hw_transfer_read_register(0x0F, SPI_MODE0),
             hw_transfer_read_register(0x2C, SPI_MODE0),
             hw_transfer_read_register(0x2D, SPI_MODE0),
             hw_transfer_read_status(SPI_MODE0));
  Serial.print(" | ");

  print_regs("HW_MODE3",
             hw_transfer_read_register(0x0E, SPI_MODE3),
             hw_transfer_read_register(0x0F, SPI_MODE3),
             hw_transfer_read_register(0x2C, SPI_MODE3),
             hw_transfer_read_register(0x2D, SPI_MODE3),
             hw_transfer_read_status(SPI_MODE3));
  Serial.println();
  Serial.flush();
}
