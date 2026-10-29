#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/l20/joints.hpp>

namespace linkerbot::hand::l20 {
    struct CurrentData {
        std::array<uint8_t, kJointCount> currents{};
        std::chrono::steady_clock::time_point timestamp;
    };
    struct CurrentLimitData {
        std::array<uint8_t, kJointCount> current_limits{};
        std::chrono::steady_clock::time_point timestamp;
    };
} // namespace linkerbot::hand::l20
