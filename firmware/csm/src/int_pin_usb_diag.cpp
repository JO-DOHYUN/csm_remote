#include <Arduino.h>

#ifndef INT_PIN_USB_DIAG_ATTACH
#define INT_PIN_USB_DIAG_ATTACH 0
#endif

static constexpr pin_size_t kMcpIntPin = D11;
static volatile uint32_t irq_edges = 0;

static void on_int_falling() {
  irq_edges++;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(kMcpIntPin, INPUT_PULLUP);

  const uint32_t start_ms = millis();
  while (!Serial && (millis() - start_ms) < 1500) {}

  Serial.println("INT_PIN_USB_DIAG boot");
  Serial.print("pin=");
  Serial.println(static_cast<int>(kMcpIntPin));
  Serial.print("interrupt=");
  Serial.println(digitalPinToInterrupt(kMcpIntPin));
  Serial.print("attach=");
  Serial.println(INT_PIN_USB_DIAG_ATTACH);

#if INT_PIN_USB_DIAG_ATTACH
  attachInterrupt(digitalPinToInterrupt(kMcpIntPin), on_int_falling, FALLING);
  Serial.println("attach_done=1");
#endif
}

void loop() {
  static uint32_t last_ms = 0;
  const uint32_t now_ms = millis();
  if (now_ms - last_ms < 250) {
    return;
  }
  last_ms = now_ms;

  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

  noInterrupts();
  const uint32_t edges = irq_edges;
  interrupts();

  Serial.print("INT_PIN_USB_DIAG ms=");
  Serial.print(now_ms);
  Serial.print(" level=");
  Serial.print(digitalRead(kMcpIntPin));
  Serial.print(" edges=");
  Serial.println(edges);
}
