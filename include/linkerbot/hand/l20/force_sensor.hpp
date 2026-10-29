#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace linkerbot::hand::l20 {

    // A single finger's tactile sensor returns a (12, 6) matrix of pressure
    // readings — 12 frames with 6 bytes each, reassembled by the manager below.
    struct ForceSensorData {
        static constexpr size_t kFrameCount = 12;
        static constexpr size_t kBytesPerFrame = 6;

        std::array<std::array<uint8_t, kBytesPerFrame>, kFrameCount> values{};
        std::chrono::steady_clock::time_point timestamp;
    };

    struct AllFingersData {
        ForceSensorData thumb;
        ForceSensorData index;
        ForceSensorData middle;
        ForceSensorData ring;
        ForceSensorData pinky;
    };

} // namespace linkerbot::hand::l20
