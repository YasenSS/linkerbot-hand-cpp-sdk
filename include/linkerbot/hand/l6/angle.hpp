#include <linkerbot/hand/errors.hpp>
#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/l6/joints.hpp>

namespace linkerbot::hand::l6 {
    enum class SampleOrigin { Query, CommandFeedback, Unknown };

    struct Angle {
        static constexpr size_t kJointCount = l6::kJointCount;

        float thumb_flex = 0.0f;
        float thumb_abd = 0.0f;
        float index = 0.0f;
        float middle = 0.0f;
        float ring = 0.0f;
        float pinky = 0.0f;

        Angle() = default;

        Angle(float tf, float ta, float i, float m, float r, float p) : Angle(from_fields({tf, ta, i, m, r, p})) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index, middle, ring, pinky};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        void validate() const { validate_angle_percents(to_array()); }

        [[nodiscard]] static Angle from_array(const std::array<float, kJointCount> &values) {
            validate_angle_percents(values);

            return from_fields(values);
        }

    private:
        static Angle from_fields(const std::array<float, kJointCount> &values) {
            Angle result;
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

    struct AngleData {
        Angle angle;
        std::chrono::steady_clock::time_point timestamp;
        uint64_t sequence = 0;
        uint64_t generation = 0;
        SampleOrigin origin = SampleOrigin::Unknown;
        std::chrono::microseconds latency{0};
        // Exact actuator-space bytes received from the hand. These are kept
        // separate from the logical Angle values so a caller can repeat the
        // current physical pose without applying the command mapping twice.
        std::array<uint8_t, Angle::kJointCount> device_raw{};
    };
} // namespace linkerbot::hand::l6
