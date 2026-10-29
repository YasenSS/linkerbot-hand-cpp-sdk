#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace linkerbot::hand::o30 {

    // MI 0x42 engineering / service fields. Dangerous operations (factory
    // restore, bootloader, firmware update) are not given named helpers
    // because their SIs were not confirmed on the current firmware.
    struct ServiceStatus {
        std::array<uint8_t, 8> password{};
        std::array<uint8_t, 5> calibration_flags{};
        uint16_t calibration_count = 0;
        std::string calibration_date;
        std::array<uint8_t, 8> comm_fault_flags{};
        uint32_t heartbeat = 0;
        uint8_t hand_side = 0;
        uint8_t sensor_type = 0;
        std::string power_on_time;
        std::string total_runtime;
        std::string motor_runtime;
        std::vector<uint8_t> task_frequency;
        std::string device_uid;
        uint8_t protocol_interface = 0;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o30
