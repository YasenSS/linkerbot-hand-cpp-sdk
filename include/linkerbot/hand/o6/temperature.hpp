#pragma once

#include <array>
#include <chrono>

namespace linkerbot::hand::o6 {

    struct Temperature {
        static constexpr size_t kJointCount = 6;

        float thumb_flex = 0.0f;
        float thumb_abd = 0.0f;
        float index = 0.0f;
        float middle = 0.0f;
        float ring = 0.0f;
        float pinky = 0.0f;

        Temperature() = default;
        Temperature(float tf, float ta, float i, float m, float r, float p)
            : thumb_flex(tf), thumb_abd(ta), index(i), middle(m), ring(r), pinky(p) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index, middle, ring, pinky};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        [[nodiscard]] static Temperature from_array(const std::array<float, kJointCount> &values) {
            return {values[0], values[1], values[2], values[3], values[4], values[5]};
        }
    };

    struct TemperatureData {
        Temperature temperature;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o6
