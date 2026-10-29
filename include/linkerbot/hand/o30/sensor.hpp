#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace linkerbot::hand::o30 {

    // MI 0x31 metadata; layout is selected from the device HOP revision.
    struct SensorInfo {
        std::string type;
        std::string unit;
        std::array<uint8_t, 6> shape_bytes{};
        uint16_t total_length = 0;
        uint8_t selected = 0;
        uint8_t rows = 0;
        uint8_t columns = 0;
        uint16_t flags = 0;
        std::chrono::steady_clock::time_point timestamp;
        // HOP 0.0.4 descriptor, e.g. FnS_U16_1;Fn_U8_70 (72 bytes).
        std::string descriptor;
        uint8_t data_range = 0;
        uint8_t maximum_rows = 0;
        uint8_t maximum_columns = 0;
    };

    struct SensorChannelData {
        uint8_t channel = 1;
        std::vector<uint8_t> payload;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct SensorReading {
        SensorInfo info;
        // All bytes of the selected sensor, in descriptor order.
        std::vector<uint8_t> payload;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct SensorData {
        SensorInfo info;
        std::vector<uint8_t> payload;
        std::chrono::steady_clock::time_point timestamp;
        // Populated only by a complete five-finger read.
        std::vector<SensorReading> sensors;
    };

    struct SensorField {
        std::string semantic;
        std::string encoding;
        size_t offset = 0;
        size_t count = 0;
        std::vector<double> values;
    };

    [[nodiscard]] std::vector<SensorField> decode_tactile_fields(const SensorReading &reading);

} // namespace linkerbot::hand::o30
