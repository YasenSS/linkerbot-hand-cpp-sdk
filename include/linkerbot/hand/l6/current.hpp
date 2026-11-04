#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace linkerbot::hand::l6 {

    struct Current {
        static constexpr size_t kJointCount = 6;

        uint8_t thumb_flex = 0;
        uint8_t thumb_abd = 0;
        uint8_t index = 0;
        uint8_t middle = 0;
        uint8_t ring = 0;
        uint8_t pinky = 0;

        Current() = default;
        Current(uint8_t tf, uint8_t ta, uint8_t i, uint8_t m, uint8_t r, uint8_t p)
            : Current(from_fields({tf, ta, i, m, r, p})) {}

        [[nodiscard]] std::array<uint8_t, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index, middle, ring, pinky};
        }

        [[nodiscard]] uint8_t operator[](size_t idx) const { return to_array().at(idx); }

        [[nodiscard]] static Current from_array(const std::array<uint8_t, kJointCount> &values) {
            return from_fields(values);
        }

    private:
        static Current from_fields(const std::array<uint8_t, kJointCount> &values) {
            Current result;
            result.thumb_flex = values[0];
            result.thumb_abd = values[1];
            result.index = values[2];
            result.middle = values[3];
            result.ring = values[4];
            result.pinky = values[5];
            return result;
        }
    };

    struct CurrentData {
        Current current;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l6
