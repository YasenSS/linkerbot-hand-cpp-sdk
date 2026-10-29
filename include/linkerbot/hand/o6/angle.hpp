#include <linkerbot/hand/errors.hpp>
#pragma once

#include <array>
#include <chrono>

#include <linkerbot/hand/o6/joints.hpp>

namespace linkerbot::hand::o6 {
    enum class SampleOrigin { Query, CommandFeedback, Unknown };

    struct Angle {
        static constexpr size_t kJointCount = o6::kJointCount;

        float thumb_flex = 0.0f;
        float thumb_abd = 0.0f;
        float index = 0.0f;
        float middle = 0.0f;
        float ring = 0.0f;
        float pinky = 0.0f;

        Angle() = default;

        Angle(float tf, float ta, float i, float m, float r, float p)
            : thumb_flex(tf), thumb_abd(ta), index(i), middle(m), ring(r), pinky(p) {
            validate();
        }

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index, middle, ring, pinky};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        void validate() const { validate_angle_percents(to_array()); }

        [[nodiscard]] static Angle from_array(const std::array<float, kJointCount> &values) {
            validate_angle_percents(values);
            return {values[0], values[1], values[2], values[3], values[4], values[5]};
        }
    };

    struct AngleData {
        Angle angle;
        std::chrono::steady_clock::time_point timestamp;
        SampleOrigin origin = SampleOrigin::Unknown;
        std::array<uint8_t, Angle::kJointCount> device_raw{};
    };
} // namespace linkerbot::hand::o6
