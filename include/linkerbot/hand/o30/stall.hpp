#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/joints.hpp>

namespace linkerbot::hand::o30 {

    struct StallTimeData {
        std::array<uint8_t, kJointCount> times{};
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct StallThresholdData {
        std::array<uint8_t, kJointCount> thresholds{};
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct StallHoldCurrentData {
        std::array<uint8_t, kJointCount> currents{};
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o30
