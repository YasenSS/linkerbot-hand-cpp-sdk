#pragma once

#include <cstdint>

namespace linkerbot::hand::l20lite {

    enum class CanIdMode : uint8_t {
        kBoth = 0x01,
        kDevice = 0x02,
        kTransmit = 0x03,
    };

    struct CanIds {
        uint8_t device_id = 0;
        uint8_t transmit_id = 0;
    };

} // namespace linkerbot::hand::l20lite
