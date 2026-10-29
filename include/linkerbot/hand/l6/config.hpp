#pragma once

#include <cstdint>

namespace linkerbot::hand::l6 {

    struct CanIds {
        uint8_t transmit_id = 0;
        uint8_t receive_id = 0;
    };

} // namespace linkerbot::hand::l6
