#include <Arduino.h>

#include "BoardPins.h"

void setup() {
  Serial.begin(115200);
  const uint32_t start_ms = millis();
  while (!Serial && millis() - start_ms < 1500) {}

  pinMode(BoardPins::CanSpiCsN, OUTPUT);
  pinMode(BoardPins::SpiCopi, OUTPUT);
  pinMode(BoardPins::SpiSck, OUTPUT);
  pinMode(BoardPins::SpiCipo, INPUT);

  digitalWrite(BoardPins::SpiCopi, LOW);
  digitalWrite(BoardPins::SpiSck, LOW);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);

  Serial.println("CS_DRIVE_DIAG_BOOT");
}

void loop() {
  static uint32_t last_ms = 0;
  static bool cs_high = true;
  const uint32_t now = millis();
  if (now - last_ms < 5000) {
    return;
  }
  last_ms = now;
  cs_high = !cs_high;
  digitalWrite(BoardPins::CanSpiCsN, cs_high ? HIGH : LOW);

  Serial.print("CS_DRIVE_DIAG cs=");
  Serial.print(cs_high ? "HIGH" : "LOW");
  Serial.print(" d7_read=");
  Serial.print(digitalRead(BoardPins::CanSpiCsN));
  Serial.print(" cipo_read=");
  Serial.println(digitalRead(BoardPins::SpiCipo));
}
