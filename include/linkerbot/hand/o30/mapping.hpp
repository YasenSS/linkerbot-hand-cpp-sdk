#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/joints.hpp>

namespace linkerbot::hand::o30 {

    // MI 0x00. Current firmware does not return the 36-byte map and rejects
    // writes of the length/enable pair with error 0x40. The API is still here
    // so a later firmware can be talked to without an SDK change.
    struct JointMapping {
        std::array<uint8_t, kSlotCount> map{};
        uint8_t length = 0;
        uint8_t enabled = 0;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o30
