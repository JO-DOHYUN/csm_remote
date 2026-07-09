#include <Arduino.h>

#include "BoardPins.h"

struct Phase {
  const char* name;
  bool cs;
  bool si;
  bool sck;
};

static constexpr Phase kPhases[] = {
    {"IDLE_CS_HIGH", true, false, false},
    {"ACTIVE_CS_LOW", false, false, false},
    {"ACTIVE_SI_HIGH", false, true, false},
    {"ACTIVE_SCK_HIGH", false, true, true},
    {"ACTIVE_ALL_LOW", false, false, false},
};

static void apply_phase(const Phase& p) {
  digitalWrite(BoardPins::CanSpiCsN, p.cs ? HIGH : LOW);
  digitalWrite(BoardPins::SpiCopi, p.si ? HIGH : LOW);
  digitalWrite(BoardPins::SpiSck, p.sck ? HIGH : LOW);
}

static void print_level(const char* name, pin_size_t pin) {
  Serial.print(' ');
  Serial.print(name);
  Serial.print('=');
  Serial.print(digitalRead(pin));
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

  apply_phase(kPhases[0]);
  Serial.println("MCP_SLOW_ACTIVE_DIAG_BOOT");
  Serial.flush();
}

void loop() {
  static uint32_t last_ms = 0;
  static uint8_t phase = 0;
  const uint32_t now = millis();
  if (now - last_ms < 2500) {
    return;
  }
  last_ms = now;
  phase = static_cast<uint8_t>((phase + 1) % (sizeof(kPhases) / sizeof(kPhases[0])));

  const Phase& p = kPhases[phase];
  apply_phase(p);

  Serial.print("MCP_SLOW phase=");
  Serial.print(p.name);
  print_level("CS_D7", BoardPins::CanSpiCsN);
  print_level("SI_D8", BoardPins::SpiCopi);
  print_level("SCK_D9", BoardPins::SpiSck);
  print_level("SO_D10", BoardPins::SpiCipo);
  print_level("INT_D11", BoardPins::CanIntN);
  Serial.println();
  Serial.flush();
}
