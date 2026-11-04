#include <linkerbot/hand/errors.hpp>
#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include <linkerbot/hand/l20lite/joints.hpp>

namespace linkerbot::hand::l20lite {

    // Stall time in milliseconds. Wire value = ms / 10 (0xC6, range 0-2550 ms).
    struct StallTime {
        static constexpr size_t kJointCount = l20lite::kJointCount;
        static constexpr float kMaxMs = 2550.0f;

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

        StallTime() = default;
        StallTime(float tf, float ta, float ix, float mf, float rf, float pf, float ia, float ra, float pa, float ty)
            : StallTime(from_fields({tf, ta, ix, mf, rf, pf, ia, ra, pa, ty})) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index_flex, middle_flex, ring_flex,
                    pinky_flex, index_abd, ring_abd,   pinky_abd,   thumb_yaw};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        void validate() const {
            for (auto v : to_array()) {
                if (std::isnan(v) || v < 0.0f || v > kMaxMs) {
                    throw ValidationError("StallTime value out of range [0, 2550] ms");
                }
            }
        }

        [[nodiscard]] static StallTime from_array(const std::array<float, kJointCount> &values) {
            return from_fields(values);
        }

    private:
        static StallTime from_fields(const std::array<float, kJointCount> &values) {
            StallTime result;
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

    struct StallTimeData {
        StallTime time;
        std::chrono::steady_clock::time_point timestamp;
    };

    // Stall threshold / hold torque as 0–100% (0xC5 / 0xC7, raw 0–255).
    struct StallLimit {
        static constexpr size_t kJointCount = l20lite::kJointCount;
        static constexpr float kMax = 100.0f;

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

        StallLimit() = default;
        StallLimit(float tf, float ta, float ix, float mf, float rf, float pf, float ia, float ra, float pa, float ty)
            : StallLimit(from_fields({tf, ta, ix, mf, rf, pf, ia, ra, pa, ty})) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index_flex, middle_flex, ring_flex,
                    pinky_flex, index_abd, ring_abd,   pinky_abd,   thumb_yaw};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        void validate() const { validate_percents(to_array(), "StallLimit"); }

        [[nodiscard]] static StallLimit from_array(const std::array<float, kJointCount> &values) {
            return from_fields(values);
        }

    private:
        static StallLimit from_fields(const std::array<float, kJointCount> &values) {
            StallLimit result;
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

    struct StallLimitData {
        StallLimit limit;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l20lite
