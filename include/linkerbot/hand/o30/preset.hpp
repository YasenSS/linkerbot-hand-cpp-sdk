#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    inline constexpr uint8_t kPresetCount = 5;

    // MI 0x36..0x3A. The first 5 bytes are the non-trigger fields; the last
    // two include the action trigger. Trigger writes were not executed on the
    // current firmware, so the SDK exposes them as raw bytes.
    struct Preset {
        uint8_t index = 1;
        std::array<uint8_t, protocol::kPresetBytes> raw{};
        std::chrono::steady_clock::time_point timestamp;

        [[nodiscard]] std::array<uint8_t, protocol::kPresetNonTriggerBytes> non_trigger() const;
    };

} // namespace linkerbot::hand::o30
