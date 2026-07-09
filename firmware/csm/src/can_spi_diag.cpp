#include <Arduino.h>
#include <SPI.h>

#include "BoardPins.h"

static uint8_t hw_read_register(uint8_t reg) {
  SPI.begin();
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  SPI.transfer(0x03);
  SPI.transfer(reg);
  const uint8_t value = SPI.transfer(0x00);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  SPI.endTransaction();
  return value;
}

static uint8_t bb_transfer(uint8_t out) {
  uint8_t in = 0;
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(BoardPins::SpiSck, LOW);
    digitalWrite(BoardPins::SpiCopi, (out & (1u << bit)) ? HIGH : LOW);
    delayMicroseconds(20);
    digitalWrite(BoardPins::SpiSck, HIGH);
    delayMicroseconds(20);
    in = static_cast<uint8_t>((in << 1) | (digitalRead(BoardPins::SpiCipo) ? 1 : 0));
  }
  digitalWrite(BoardPins::SpiSck, LOW);
  delayMicroseconds(20);
  return in;
}

static uint8_t bb_read_register(uint8_t reg) {
  pinMode(BoardPins::CanSpiCsN, OUTPUT);
  pinMode(BoardPins::SpiCopi, OUTPUT);
  pinMode(BoardPins::SpiSck, OUTPUT);
  pinMode(BoardPins::SpiCipo, INPUT);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  digitalWrite(BoardPins::SpiSck, LOW);
  digitalWrite(BoardPins::SpiCopi, LOW);
  delayMicroseconds(50);
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  delayMicroseconds(50);
  bb_transfer(0x03);
  bb_transfer(reg);
  const uint8_t value = bb_transfer(0x00);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  delayMicroseconds(50);
  return value;
}

static void bb_reset() {
  pinMode(BoardPins::CanSpiCsN, OUTPUT);
  pinMode(BoardPins::SpiCopi, OUTPUT);
  pinMode(BoardPins::SpiSck, OUTPUT);
  pinMode(BoardPins::SpiCipo, INPUT);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  digitalWrite(BoardPins::SpiSck, LOW);
  digitalWrite(BoardPins::SpiCopi, LOW);
  delayMicroseconds(50);
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  delayMicroseconds(50);
  bb_transfer(0xC0);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  delay(10);
}

void setup() {
  Serial.begin(115200);
  const uint32_t start_ms = millis();
  while (!Serial && millis() - start_ms < 1500) {}

  pinMode(BoardPins::CanSpiCsN, OUTPUT);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  pinMode(BoardPins::SpiCopi, OUTPUT);
  pinMode(BoardPins::SpiSck, OUTPUT);
  pinMode(BoardPins::SpiCipo, INPUT);
  digitalWrite(BoardPins::SpiCopi, LOW);
  digitalWrite(BoardPins::SpiSck, LOW);
  Serial.println("CAN_SPI_DIAG_BOOT");
  Serial.flush();
}

void loop() {
  static uint32_t last_ms = 0;
  if (millis() - last_ms < 1000) {
    return;
  }
  last_ms = millis();

  const uint8_t idle_miso = digitalRead(BoardPins::SpiCipo) ? 1 : 0;
  bb_reset();
  const uint8_t bb_stat = bb_read_register(0x0E);
  const uint8_t bb_ctrl = bb_read_register(0x0F);
  const uint8_t bb_intf = bb_read_register(0x2C);
  const uint8_t bb_eflg = bb_read_register(0x2D);

  Serial.print("CAN_SPI_DIAG_BB idle_miso=");
  Serial.print(idle_miso);
  Serial.print(" bb=");
  Serial.print(bb_stat, HEX);
  Serial.print(",");
  Serial.print(bb_ctrl, HEX);
  Serial.print(",");
  Serial.print(bb_intf, HEX);
  Serial.print(",");
  Serial.println(bb_eflg, HEX);
  Serial.flush();

  const uint8_t hw_stat = hw_read_register(0x0E);
  const uint8_t hw_ctrl = hw_read_register(0x0F);
  const uint8_t hw_intf = hw_read_register(0x2C);
  const uint8_t hw_eflg = hw_read_register(0x2D);

  Serial.print("CAN_SPI_DIAG_HW hw=");
  Serial.print(hw_stat, HEX);
  Serial.print(",");
  Serial.print(hw_ctrl, HEX);
  Serial.print(",");
  Serial.print(hw_intf, HEX);
  Serial.print(",");
  Serial.println(hw_eflg, HEX);
  Serial.flush();
}
