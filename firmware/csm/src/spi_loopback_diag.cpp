#include <Arduino.h>
#include <SPI.h>

#include "BoardPins.h"

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
  return in;
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

  SPI.begin();
  Serial.println("SPI_LOOPBACK_DIAG_BOOT");
}

void loop() {
  static uint32_t last_ms = 0;
  if (millis() - last_ms < 1000) {
    return;
  }
  last_ms = millis();

  const uint8_t patterns[] = {0x00, 0xFF, 0xA5, 0x5A, 0x3C, 0xC3};

  Serial.print("SPI_LOOPBACK_BB");
  bool bb_ok = true;
  for (uint8_t p : patterns) {
    const uint8_t r = bb_transfer(p);
    bb_ok = bb_ok && (r == p);
    Serial.print(" ");
    Serial.print(p, HEX);
    Serial.print("->");
    Serial.print(r, HEX);
  }
  Serial.print(" ok=");
  Serial.println(bb_ok ? 1 : 0);

  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  Serial.print("SPI_LOOPBACK_HW");
  bool hw_ok = true;
  for (uint8_t p : patterns) {
    const uint8_t r = SPI.transfer(p);
    hw_ok = hw_ok && (r == p);
    Serial.print(" ");
    Serial.print(p, HEX);
    Serial.print("->");
    Serial.print(r, HEX);
  }
  SPI.endTransaction();
  Serial.print(" ok=");
  Serial.println(hw_ok ? 1 : 0);
}
