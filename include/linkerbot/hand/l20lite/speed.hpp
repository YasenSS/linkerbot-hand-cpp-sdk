#pragma once

#include <array>
#include <chrono>

#include <linkerbot/hand/l20lite/joints.hpp>

namespace linkerbot::hand::l20lite {

    struct Speed {
        static constexpr size_t kJointCount = l20lite::kJointCount;

        float thumb_flex = 0.0f;
        float thumb_abd = 0.0f;
        float index_flex = 0.0f;
        float middle_flex = 0.0f;
        float ring_flex = 0.0f;
        float pinky_flex = 0.0f;
        float index_abd = 0.0f;
        float ring_abd = 0.0f;
        float pinky_abd = 0.0f;
        float thumb_yaw = 0.0f;

        Speed() = default;
        Speed(float tf, float ta, float ix, float mf, float rf, float pf, float ia, float ra, float pa, float ty)
            : Speed(from_fields({tf, ta, ix, mf, rf, pf, ia, ra, pa, ty})) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index_flex, middle_flex, ring_flex,
                    pinky_flex, index_abd, ring_abd,   pinky_abd,   thumb_yaw};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        void validate() const { validate_percents(to_array(), "Speed"); }

        [[nodiscard]] static Speed from_array(const std::array<float, kJointCount> &values) {
            validate_percents(values, "Speed");

            return from_fields(values);
        }

    private:
        static Speed from_fields(const std::array<float, kJointCount> &values) {
            Speed result;
            result.thumb_flex = values[0];
            result.thumb_abd = values[1];
            result.index_flex = values[2];
            result.middle_flex = values[3];
            result.ring_flex = values[4];
            result.pinky_flex = values[5];
            result.index_abd = values[6];
            result.ring_abd = values[7];
            result.pinky_abd = values[8];
            result.thumb_yaw = values[9];
            result.validate();
            return result;
        }
    };

    struct SpeedData {
        Speed speed;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l20lite
