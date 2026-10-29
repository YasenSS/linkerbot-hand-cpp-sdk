#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace linkerbot::hand::l6 {

    struct ForceSensorData {
        static constexpr size_t kFrameCount = 12;
        static constexpr size_t kBytesPerFrame = 6;

        std::array<std::array<uint8_t, kBytesPerFrame>, kFrameCount> values{};
        std::chrono::steady_clock::time_point timestamp;
        uint64_t sequence = 0;
        uint64_t generation = 0;
        std::chrono::microseconds latency{0};
    };

    struct AllFingersData {
        ForceSensorData thumb;
        ForceSensorData index;
        ForceSensorData middle;
        ForceSensorData ring;
        ForceSensorData pinky;
        std::chrono::steady_clock::time_point timestamp;
        uint64_t sequence = 0;
        uint64_t generation = 0;
        std::chrono::microseconds latency{0};
    };

} // namespace linkerbot::hand::l6
