#include <Arduino.h>

#include "BoardPins.h"

static void print_input_truth(const char* name, pin_size_t pin) {
  Serial.print(name);
  Serial.print(" plain=");
  pinMode(pin, INPUT);
  delayMicroseconds(200);
  Serial.print(digitalRead(pin));

  Serial.print(" pullup=");
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(200);
  Serial.print(digitalRead(pin));

#ifdef INPUT_PULLDOWN
  Serial.print(" pulldown=");
  pinMode(pin, INPUT_PULLDOWN);
  delayMicroseconds(200);
  Serial.print(digitalRead(pin));
#else
  Serial.print(" pulldown=NA");
#endif
}

static void drive_outputs(bool cs, bool si, bool sck) {
  pinMode(BoardPins::CanSpiCsN, OUTPUT);
  pinMode(BoardPins::SpiCopi, OUTPUT);
  pinMode(BoardPins::SpiSck, OUTPUT);
  digitalWrite(BoardPins::CanSpiCsN, cs ? HIGH : LOW);
  digitalWrite(BoardPins::SpiCopi, si ? HIGH : LOW);
  digitalWrite(BoardPins::SpiSck, sck ? HIGH : LOW);
  delay(10);
}

static void print_outputs() {
  Serial.print(" out CS=");
  Serial.print(digitalRead(BoardPins::CanSpiCsN));
  Serial.print(" SI=");
  Serial.print(digitalRead(BoardPins::SpiCopi));
  Serial.print(" SCK=");
  Serial.print(digitalRead(BoardPins::SpiSck));
}

void setup() {
  Serial.begin(115200);
  const uint32_t start_ms = millis();
  while (!Serial && millis() - start_ms < 1500) {}

  drive_outputs(true, false, false);
  Serial.println("MCP_PIN_TRUTH_DIAG_BOOT");
  Serial.flush();
}

void loop() {
  static uint32_t last_ms = 0;
  static uint8_t phase = 0;
  const uint32_t now = millis();
  if (now - last_ms < 1500) {
    return;
  }
  last_ms = now;
  phase = static_cast<uint8_t>((phase + 1) % 4);

  const char* label = "ALL_LOW";
  if (phase == 0) {
    drive_outputs(false, false, false);
  } else if (phase == 1) {
    drive_outputs(true, false, false);
    label = "CS_HIGH";
  } else if (phase == 2) {
    drive_outputs(false, true, false);
    label = "SI_HIGH";
  } else {
    drive_outputs(false, false, true);
    label = "SCK_HIGH";
  }

  Serial.print("MCP_PIN_TRUTH phase=");
  Serial.print(label);
  print_outputs();
  Serial.print(" | ");
  print_input_truth("SO_D10", BoardPins::SpiCipo);
  Serial.print(" | ");
  print_input_truth("INT_D11", BoardPins::CanIntN);
  Serial.println();
  Serial.flush();
}
