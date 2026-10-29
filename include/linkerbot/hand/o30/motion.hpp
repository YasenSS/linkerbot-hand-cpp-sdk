#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/joints.hpp>

namespace linkerbot::hand::o30 {

    struct SpeedData {
        std::array<uint8_t, kJointCount> speeds{};
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct AccelerationData {
        std::array<uint8_t, kJointCount> accelerations{};
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct CurrentData {
        std::array<uint8_t, kJointCount> currents{};
        std::chrono::steady_clock::time_point timestamp;
    };
    struct CurrentLimitData {
        std::array<uint8_t, kJointCount> current_limits{};
        std::chrono::steady_clock::time_point timestamp;
    };

    struct VoltageData {
        std::array<uint8_t, kJointCount> voltages{};
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct TorqueData {
        std::array<uint8_t, kJointCount> torques{};
        std::chrono::steady_clock::time_point timestamp;
    };

    struct TemperatureData {
        std::array<uint8_t, kJointCount> temperatures{};
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o30
