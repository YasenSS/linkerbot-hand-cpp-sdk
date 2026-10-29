#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include <linkerbot/hand/l30/joints.hpp>

namespace linkerbot::hand::l30 {

    // Joint angles as 0-100 percentages in J1..J17 order.
    //
    // Sensor readback can land slightly outside [0, 100] when the hardware
    // overshoots, so this type does not clamp. Command paths validate separately.
    struct Angle {
        std::array<float, kJointCount> values{};

        [[nodiscard]] float operator[](size_t index) const { return values.at(index); }

        [[nodiscard]] std::array<int16_t, kJointCount> to_raw() const { return percents_to_raw(values); }

        // Throws std::invalid_argument unless every entry is within [0, 100].
        void validate() const { validate_percents(values, "angles"); }

        [[nodiscard]] static Angle from_array(const std::array<float, kJointCount> &values);

        // Sensor-derived percentages are accepted as-is, without range checking.
        [[nodiscard]] static Angle from_sensor_raw(const std::array<int16_t, kJointCount> &raw);
    };

    struct AngleData {
        Angle angles;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l30
