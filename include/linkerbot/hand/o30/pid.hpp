#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    // Documented as 72-byte half-word vectors; current firmware only returns
    // 36 bytes and rejects SI >= 0x24. The SDK treats them as 36-byte objects.
    enum class PidTerm {
        kPositionP = protocol::kMiPositionPidP,
        kPositionI = protocol::kMiPositionPidI,
        kPositionD = protocol::kMiPositionPidD,
        kSpeedP = protocol::kMiSpeedPidP,
        kSpeedI = protocol::kMiSpeedPidI,
        kSpeedD = protocol::kMiSpeedPidD
    };

    struct PidData {
        PidTerm term = PidTerm::kPositionP;
        std::array<uint8_t, kJointCount> values{};
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o30
