#pragma once

#include <stdint.h>

namespace csm::board::remote {

// Frozen product wiring/profile for RadioLink R16SM + T16D. Channel indexes
// are zero based. Physical baud measurement remains qualification evidence;
// runtime uses the proven configured rate and never auto-probes/retries.
static constexpr uint8_t kR16smCrsfAddress = 0xC8u;
static constexpr uint32_t kR16smConfiguredBaud = 416666u;
static constexpr uint8_t kR16smDriveChannelIndex = 3u;       // CH4.
static constexpr uint8_t kR16smSteeringChannelIndex = 1u;    // CH2.
static constexpr uint8_t kR16smAuxiliaryChannelIndex = 4u;   // CH5.
static constexpr uint8_t kR16smSteeringOverlayChannelIndex = 9u;  // CH10.
static constexpr uint8_t kR16smMomentaryOverlayChannelIndex = 10u; // CH11.
static constexpr uint16_t kR16smRequiredControlChannelMask =
    (1u << kR16smDriveChannelIndex) |
    (1u << kR16smSteeringChannelIndex);
static constexpr uint16_t kR16smOptionalFunctionChannelMask =
    (1u << kR16smAuxiliaryChannelIndex) |
    (1u << kR16smSteeringOverlayChannelIndex) |
    (1u << kR16smMomentaryOverlayChannelIndex);
static constexpr uint16_t kR16smSixteenChannelMask = 0xFFFFu;
static constexpr uint8_t kR16smAdmissionConsecutiveFrames = 3u;
static constexpr uint32_t kR16smRcFreshnessMs = 100u;
static constexpr uint32_t kR16smLinkStatisticsFreshnessMs = 500u;

}  // namespace csm::board::remote
