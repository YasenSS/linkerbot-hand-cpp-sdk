#pragma once

#include <cstdint>

namespace linkerbot::hand::o6 {

    enum class CanIdMode : uint8_t {
        kBoth = 0x01,
        kDevice = 0x02,
        kTransmit = 0x03,
    };

} // namespace linkerbot::hand::o6
