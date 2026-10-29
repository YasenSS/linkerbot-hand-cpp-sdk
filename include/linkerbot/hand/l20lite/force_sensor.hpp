#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace linkerbot::hand::l20lite {

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

} // namespace linkerbot::hand::l20lite
