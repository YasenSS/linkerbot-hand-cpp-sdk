#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    // MI 0x4F. A solicited 4F response is the error-history object, not a
    // failed request.
    struct CommErrorData {
        uint8_t latest_index = 0;
        // Exact returned history: observed firmware exposes 15 or 16 bytes.
        // Never treat CAN FD padding as an extra history entry.
        std::vector<uint8_t> history;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o30
