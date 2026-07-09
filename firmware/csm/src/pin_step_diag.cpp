#include <Arduino.h>

#include "BoardPins.h"

struct PinDiag {
  const char* name;
  pin_size_t pin;
  bool output;
};

static constexpr PinDiag kPins[] = {
    {"D7_CS", BoardPins::CanSpiCsN, true},
    {"D8_MOSI_COPI", BoardPins::SpiCopi, true},
    {"D9_SCK", BoardPins::SpiSck, true},
    {"D10_MISO_CIPO", BoardPins::SpiCipo, false},
    {"D11_INT", BoardPins::CanIntN, false},
};

static void print_pin_level(const PinDiag& p) {
  Serial.print(p.name);
  Serial.print(" pin=");
  Serial.print(static_cast<int>(p.pin));
  Serial.print(" level=");
  Serial.println(digitalRead(p.pin));
}

void setup() {
  Serial.begin(115200);
  const uint32_t start_ms = millis();
  while (!Serial && millis() - start_ms < 1500) {}

  Serial.println("PIN_STEP_DIAG_BOOT");
  Serial.flush();

  for (const auto& p : kPins) {
    Serial.print("config ");
    Serial.println(p.name);
    Serial.flush();
    if (p.output) {
      pinMode(p.pin, OUTPUT);
      digitalWrite(p.pin, LOW);
    } else {
      pinMode(p.pin, INPUT_PULLUP);
    }
    delay(100);
    print_pin_level(p);
    Serial.flush();
  }

  Serial.println("PIN_STEP_DIAG_READY");
  Serial.flush();
}

void loop() {
#if defined(PIN_STEP_DIAG_HOLD_SPI_HIGH)
  static uint32_t last_ms = 0;
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  digitalWrite(BoardPins::SpiCopi, HIGH);
  digitalWrite(BoardPins::SpiSck, HIGH);
  const uint32_t now = millis();
  if (now - last_ms < 1000) {
    return;
  }
  last_ms = now;
  Serial.println("PIN_STEP_DIAG phase=HOLD_D7_D8_D9_HIGH");
  for (const auto& p : kPins) {
    print_pin_level(p);
  }
  Serial.flush();
#elif defined(PIN_STEP_DIAG_HOLD_CS_HIGH)
  static uint32_t last_ms = 0;
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  digitalWrite(BoardPins::SpiCopi, LOW);
  digitalWrite(BoardPins::SpiSck, LOW);
  const uint32_t now = millis();
  if (now - last_ms < 1000) {
    return;
  }
  last_ms = now;
  Serial.println("PIN_STEP_DIAG phase=HOLD_D7_CS_HIGH");
  for (const auto& p : kPins) {
    print_pin_level(p);
  }
  Serial.flush();
#else
  static uint32_t last_ms = 0;
  static uint8_t phase = 0;
  const uint32_t now = millis();
  if (now - last_ms < 1500) {
    return;
  }
  last_ms = now;
  phase = static_cast<uint8_t>((phase + 1) % 5);

  digitalWrite(BoardPins::CanSpiCsN, LOW);
  digitalWrite(BoardPins::SpiCopi, LOW);
  digitalWrite(BoardPins::SpiSck, LOW);

  const char* label = "ALL_LOW";
  if (phase == 1) {
    digitalWrite(BoardPins::CanSpiCsN, HIGH);
    label = "D7_CS_HIGH_ONLY";
  } else if (phase == 2) {
    digitalWrite(BoardPins::SpiCopi, HIGH);
    label = "D8_MOSI_HIGH_ONLY";
  } else if (phase == 3) {
    digitalWrite(BoardPins::SpiSck, HIGH);
    label = "D9_SCK_HIGH_ONLY";
  } else if (phase == 4) {
    digitalWrite(BoardPins::CanSpiCsN, HIGH);
    digitalWrite(BoardPins::SpiCopi, HIGH);
    digitalWrite(BoardPins::SpiSck, HIGH);
    label = "D7_D8_D9_HIGH";
  }

  Serial.print("PIN_STEP_DIAG phase=");
  Serial.println(label);
  for (const auto& p : kPins) {
    print_pin_level(p);
  }
  Serial.flush();
#endif
}
