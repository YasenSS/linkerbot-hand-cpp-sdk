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
            : Temperature(from_fields({tf, ta, i, m, r, p})) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index, middle, ring, pinky};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        [[nodiscard]] static Temperature from_array(const std::array<float, kJointCount> &values) {
            return from_fields(values);
        }

    private:
        static Temperature from_fields(const std::array<float, kJointCount> &values) {
            Temperature result;
            result.thumb_flex = values[0];
            result.thumb_abd = values[1];
            result.index = values[2];
            result.middle = values[3];
            result.ring = values[4];
            result.pinky = values[5];
            return result;
        }
    };

    struct TemperatureData {
        Temperature temperature;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o6
