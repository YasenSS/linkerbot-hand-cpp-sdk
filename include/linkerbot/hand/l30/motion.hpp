#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include <linkerbot/hand/l30/joints.hpp>

namespace linkerbot::hand::l30 {

    // Per-joint speed in raw protocol counts (1..250, 0.732 rpm per count).
    // The API stays in counts, matching the Python SDK; multiply by
    // kSpeedRpmPerCount if you want rpm.
    struct SpeedData {
        std::array<uint16_t, kJointCount> speeds{};
        std::chrono::steady_clock::time_point timestamp;
    };

    // Per-joint current-limit target in raw counts (60..800, 6.5 mA per count).
    struct CurrentLimitData {
        std::array<uint16_t, kJointCount> current_limits{};
        std::chrono::steady_clock::time_point timestamp;
    };

    // Measured per-joint current in raw counts (6.5 mA per count). Signed: the
    // sign carries the direction of the motor's effort.
    struct CurrentData {
        std::array<int16_t, kJointCount> currents{};
        std::chrono::steady_clock::time_point timestamp;
    };

    // Per-joint temperature, 1 degree Celsius per count.
    struct TemperatureData {
        std::array<uint8_t, kJointCount> temperatures{};
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l30
