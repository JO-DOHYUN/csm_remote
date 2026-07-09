#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  static uint32_t last_ms = 0;
  const uint32_t now_ms = millis();
  if (now_ms - last_ms >= 500) {
    last_ms = now_ms;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    Serial.println("PORTENTA_MIN_USB_OK");
  }
}
