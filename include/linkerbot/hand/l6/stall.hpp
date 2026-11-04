#include <linkerbot/hand/errors.hpp>
#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace linkerbot::hand::l6 {

    // Stall time in milliseconds. Wire value = round(ms / 10).
    // Readback can contain 0; writes must encode to raw 1..255 (0 is ineffective).
    struct StallTime {
        static constexpr size_t kJointCount = 6;
        static constexpr float kMaxMs = 2550.0f;

        float thumb_flex = 0.0f;
        float thumb_abd = 0.0f;
        float index = 0.0f;
        float middle = 0.0f;
        float ring = 0.0f;
        float pinky = 0.0f;

        StallTime() = default;
        StallTime(float tf, float ta, float i, float m, float r, float p)
            : StallTime(from_fields({tf, ta, i, m, r, p})) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index, middle, ring, pinky};
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
            result.index = values[2];
            result.middle = values[3];
            result.ring = values[4];
            result.pinky = values[5];
            result.validate();
            return result;
        }
    };

    struct StallTimeData {
        StallTime time;
        std::chrono::steady_clock::time_point timestamp;
    };

    // Stall current/torque limit in the protocol's 0-1400 unit (0xC6 / 0xC7).
    struct StallLimit {
        static constexpr size_t kJointCount = 6;
        static constexpr float kMax = 1400.0f;

        float thumb_flex = 0.0f;
        float thumb_abd = 0.0f;
        float index = 0.0f;
        float middle = 0.0f;
        float ring = 0.0f;
        float pinky = 0.0f;

        StallLimit() = default;
        StallLimit(float tf, float ta, float i, float m, float r, float p)
            : StallLimit(from_fields({tf, ta, i, m, r, p})) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index, middle, ring, pinky};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        void validate() const {
            for (auto v : to_array()) {
                if (std::isnan(v) || v < 0.0f || v > kMax) {
                    throw ValidationError("StallLimit value out of range [0, 1400]");
                }
            }
        }

        [[nodiscard]] static StallLimit from_array(const std::array<float, kJointCount> &values) {
            return from_fields(values);
        }

    private:
        static StallLimit from_fields(const std::array<float, kJointCount> &values) {
            StallLimit result;
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

    struct StallLimitData {
        StallLimit limit;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l6
