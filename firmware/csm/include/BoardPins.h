#pragma once

#include <Arduino.h>

namespace BoardPins {

// Current Mid Carrier CSM target:
// - bus0: external MCP2515 + TJA1050 module over SPI on D7..D11.
// - CS/SCK/SI are Portenta -> MCP signals and SO/INT are MCP -> Portenta
//   signals; 5 V MCP modules require level shifting in the correct direction.
// - The earlier internal CAN0/CAN1 + TJA1051 direction is deferred until a
//   second internal CAN backend is implemented and HIL-proven.

static constexpr pin_size_t SafetyWatchdogToggle = D0;  // PH15
static constexpr pin_size_t CanTxEnable = D1;           // PK1, default disabled by hardware pulldown
static constexpr pin_size_t EstopInN = D2;              // PJ11, isolated input
static constexpr pin_size_t ArmKeyIn = D3;              // PG7, isolated/service input

static constexpr pin_size_t EncoderB = D4;              // PC7, TIM3_CH2
static constexpr pin_size_t EncoderA = D5;              // PC6, TIM3_CH1
static constexpr pin_size_t EncoderZ = D6;              // PA8, EXTI8

namespace LegacyMcp2515Pins {
static constexpr pin_size_t CanSpiCsN = D7;             // PI0, MCP2515 CS
static constexpr pin_size_t SpiCopi = D8;               // PC3, SPI MOSI -> MCP2515 SI
static constexpr pin_size_t SpiSck = D9;                // PI1, SPI SCK
static constexpr pin_size_t SpiCipo = D10;              // PC2, SPI MISO <- MCP2515 SO
static constexpr pin_size_t CanIntN = D11;              // PH8, MCP2515 INT
}  // namespace LegacyMcp2515Pins

// Backward-compatible aliases for the current legacy MCP bench diagnostics.
static constexpr pin_size_t CanSpiCsN = LegacyMcp2515Pins::CanSpiCsN;
static constexpr pin_size_t SpiCopi = LegacyMcp2515Pins::SpiCopi;
static constexpr pin_size_t SpiSck = LegacyMcp2515Pins::SpiSck;
static constexpr pin_size_t SpiCipo = LegacyMcp2515Pins::SpiCipo;
static constexpr pin_size_t CanIntN = LegacyMcp2515Pins::CanIntN;

static constexpr pin_size_t FieldPowerOk = D12;         // PH7
static constexpr pin_size_t EncoderFaultN = D13;        // PA10
static constexpr pin_size_t SpareServiceGpio = D14;     // PA9
static constexpr pin_size_t AdcSpiCsN = D14;            // PA9, future external ADC CS
static constexpr pin_size_t AdcDataReadyN = D14;        // future ADC DRDY/shared spare

// Lab voltage evidence inputs. Avoid A2/A3/A4/A5 in the MCP bench because
// those Portenta analog aliases share the PC2/PC3 SPI pins used by D10/D8.
static constexpr pin_size_t VoltageSense0 = A0;         // PA0C, ADC2_INP0
static constexpr pin_size_t VoltageSense1 = A1;         // PA1C, ADC2_INP1
static constexpr pin_size_t VoltageSense2 = A6;         // PA4, ADC1_INP18
static constexpr pin_size_t VoltageSense3 = A7;         // PA6, ADC1_INP7

}  // namespace BoardPins
