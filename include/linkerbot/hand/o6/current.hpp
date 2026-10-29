#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace linkerbot::hand::o6 {
    inline constexpr size_t kCurrentJointCount = 6;

    struct CurrentData {
        std::array<uint8_t, kCurrentJointCount> currents{};
        std::chrono::steady_clock::time_point timestamp;
    };

    struct CurrentLimitData {
        std::array<uint8_t, kCurrentJointCount> current_limits{};
        std::chrono::steady_clock::time_point timestamp;
    };
} // namespace linkerbot::hand::o6
