#pragma once

#include <array>
#include <chrono>

#include <linkerbot/hand/l20lite/joints.hpp>

namespace linkerbot::hand::l20lite {

    struct Angle {
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

        Angle() = default;
        Angle(float tf, float ta, float ix, float mf, float rf, float pf, float ia, float ra, float pa, float ty)
            : thumb_flex(tf), thumb_abd(ta), index_flex(ix), middle_flex(mf), ring_flex(rf), pinky_flex(pf),
              index_abd(ia), ring_abd(ra), pinky_abd(pa), thumb_yaw(ty) {
            validate();
        }

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index_flex, middle_flex, ring_flex,
                    pinky_flex, index_abd, ring_abd,   pinky_abd,   thumb_yaw};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        void validate() const { validate_percents(to_array(), "Angle"); }

        [[nodiscard]] static Angle from_array(const std::array<float, kJointCount> &values) {
            validate_percents(values, "Angle");
            return {values[0], values[1], values[2], values[3], values[4],
                    values[5], values[6], values[7], values[8], values[9]};
        }
    };

    struct AngleData {
        Angle angle;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l20lite
