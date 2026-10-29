#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace linkerbot::hand::l20 {

    struct Version {
        uint8_t major = 0;
        uint8_t minor = 0;
        uint8_t patch = 0;

        [[nodiscard]] std::string to_string() const {
            return "V" + std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
        }
    };

    struct DeviceInfo {
        std::string serial_number;
        Version pcb_version;
        Version firmware_version;
        Version mechanical_version;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l20
