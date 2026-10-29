#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    // Legacy value type only. EnableManager does not perform protocol I/O;
    // 0.0.3 masks are ineffective; 0.0.4 MI 0x0C is the fault object.
    struct EnableMask {
        uint8_t bits = 0;

        [[nodiscard]] bool exists() const { return (bits & protocol::kEnableExists) != 0U; }
        [[nodiscard]] bool controllable() const { return (bits & protocol::kEnableControllable) != 0U; }
        [[nodiscard]] bool enabled() const { return (bits & protocol::kEnableOn) != 0U; }
        [[nodiscard]] bool feedback() const { return (bits & protocol::kEnableFeedback) != 0U; }
        [[nodiscard]] bool calibratable() const { return (bits & protocol::kEnableCalibrate) != 0U; }
    };

    struct EnableData {
        std::array<EnableMask, kJointCount> joints{};
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o30
