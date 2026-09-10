#pragma once
#include "../native_stubs/Arduino.h"
#include <stdint.h>
inline uint32_t observation_test_ms=1;
inline unsigned long millis() { return observation_test_ms; }
inline unsigned long micros() { return observation_test_ms*1000u; }
