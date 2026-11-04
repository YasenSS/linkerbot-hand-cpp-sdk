#include <linkerbot/hand/errors.hpp>
#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace linkerbot::hand::o6 {

    struct Speed {
        static constexpr size_t kJointCount = 6;

        float thumb_flex = 0.0f;
        float thumb_abd = 0.0f;
        float index = 0.0f;
        float middle = 0.0f;
        float ring = 0.0f;
        float pinky = 0.0f;

        Speed() = default;
        Speed(float tf, float ta, float i, float m, float r, float p) : Speed(from_fields({tf, ta, i, m, r, p})) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index, middle, ring, pinky};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        void validate() const {
            for (auto v : to_array()) {
                if (std::isnan(v) || v < 0.0f || v > 100.0f) {
                    throw ValidationError("Speed value out of range [0, 100]");
                }
            }
        }

        [[nodiscard]] static Speed from_array(const std::array<float, kJointCount> &values) {
            for (auto v : values) {
                if (std::isnan(v) || v < 0.0f || v > 100.0f) {
                    throw ValidationError("Speed value out of range [0, 100]");
                }
            }

            return from_fields(values);
        }

    private:
        static Speed from_fields(const std::array<float, kJointCount> &values) {
            Speed result;
            result.thumb_flex = values[0];
            result.thumb_abd = values[1];
            result.index = values[2];
            result.middle = values[3];
            result.ring = values[4];
            result.pinky = values[5];
            result.validate();
            return result;
        }
    };

    struct SpeedData {
        Speed speed;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o6
