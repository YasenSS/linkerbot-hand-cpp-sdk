#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace linkerbot::hand::o30 {

    // MI 0x43. Select a target MI, then read the unit string and per-slot
    // ranges. Current firmware often reports the unit as "normalization" and
    // ranges as 0x0000..0x00FF.
    struct UnitInfo {
        uint8_t target_mi = 0;
        std::string unit;
        std::array<uint8_t, 17> raw{};
        std::chrono::steady_clock::time_point timestamp;
        uint8_t decimal_places = 0; // Byte 16, separate from the 16-byte unit string.
    };

    struct RangeInfo {
        uint8_t target_mi = 0;
        uint8_t si = 0;
        uint16_t low = 0;
        uint16_t high = 0;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o30
