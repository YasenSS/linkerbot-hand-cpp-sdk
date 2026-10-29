#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include <linkerbot/hand/o30/joints.hpp>

namespace linkerbot::hand::o30 {

    // Joint positions as 0-100 percentages in the 20-joint SDK order.
    //
    // MI 0x01, one byte per slot. Flexion (6..19): 0 % = 0xFF (flexed),
    // 100 % = 0x00 (extended). Other axes: 0 % = 0x00, 100 % = 0xFF.
    // Thumb rotation (0): away -> toward
    // index. Thumb roll (1): parallel -> perpendicular to palm. Four-finger
    // abduction (2..5): toward -> away from thumb. Same on both hands.
    // These are normalized positions, not degrees. Commands validate separately.
    struct Angle {
        std::array<float, kJointCount> values{};

        [[nodiscard]] float operator[](size_t index) const { return values.at(index); }

        [[nodiscard]] std::array<uint8_t, kJointCount> to_raw() const { return percents_to_raw(values); }

        void validate() const { validate_percents(values, "angles"); }

        [[nodiscard]] static Angle from_array(const std::array<float, kJointCount> &values);

        [[nodiscard]] static Angle from_sensor_raw(const std::array<uint8_t, kJointCount> &raw);
    };

    struct AngleData {
        Angle angles;
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
    };

    // MI 0x20: 16-bit little-endian positions, 36 slots / 72 bytes.
    // Current firmware only accepts a full-object read.
    struct HalfwordPositionData {
        std::array<uint16_t, kJointCount> positions{};
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o30
