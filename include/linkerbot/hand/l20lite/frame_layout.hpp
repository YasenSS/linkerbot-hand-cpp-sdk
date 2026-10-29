#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <linkerbot/hand/l20lite/joints.hpp>
namespace linkerbot::hand::l20lite {
    struct FramePart {
        uint8_t cmd = 0;
        std::array<size_t, 6> joints{};
        size_t count = 0;
    };

    inline constexpr FramePart kAngleFrameA{0x01, {0, 1, 2, 3, 4, 5}, 6};
    inline constexpr FramePart kAngleFrameB{0x04, {6, 7, 8, 9}, 4};
    inline constexpr FramePart kCurrentFrameA{0x02, {0, 1, 2, 3, 4}, 5};
    inline constexpr FramePart kCurrentFrameB{0x03, {5, 6, 7, 8, 9}, 5};
    inline constexpr FramePart kSpeedFrameA{0x05, {0, 1, 2, 3, 4}, 5};
    inline constexpr FramePart kSpeedFrameB{0x06, {5, 6, 7, 8, 9}, 5};
    inline constexpr FramePart kAccelFrameA{0x87, {0, 1, 2, 3, 4}, 5};
    inline constexpr FramePart kAccelFrameB{0x88, {5, 6, 7, 8, 9}, 5};
    inline constexpr FramePart kTempFrameA{0x33, {0, 1, 2, 3, 4}, 5};
    inline constexpr FramePart kTempFrameB{0x34, {5, 6, 7, 8, 9}, 5};
    inline constexpr FramePart kFaultFrameA{0x35, {0, 1, 2, 3, 4}, 5};
    inline constexpr FramePart kFaultFrameB{0x36, {5, 6, 7, 8, 9}, 5};

    // LHT10 vector banks require 1 ms spacing. V4.6.10 intermittently
    // omits replies at 0.5 ms or when the banks are sent back-to-back.
    inline constexpr auto kVectorInterFrameDelay = std::chrono::microseconds{1000};
    inline constexpr auto kAngleInterFrameDelay = kVectorInterFrameDelay;
    inline constexpr auto kForceInterRequestDelay = std::chrono::microseconds{2500};

}
