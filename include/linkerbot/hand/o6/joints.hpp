#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <linkerbot/hand/errors.hpp>

namespace linkerbot::hand::o6 {
    inline constexpr size_t kJointCount = 6;
    inline constexpr float kAnglePercentMinimum = 0.0F;
    inline constexpr float kAnglePercentMaximum = 100.0F;
    inline constexpr uint8_t kRawAngleMinimum = 0;
    inline constexpr uint8_t kRawAngleMaximum = 255;

    inline constexpr std::array<std::string_view, kJointCount> kJointNames = {"thumb_flex", "thumb_abd", "index",
                                                                              "middle",     "ring",      "pinky"};

    inline void validate_angle_percents(const std::array<float, kJointCount> &values) {
        for (const auto value : values) {
            if (!std::isfinite(value) || value < kAnglePercentMinimum || value > kAnglePercentMaximum) {
                throw ValidationError("Angle value out of range [0, 100]");
            }
        }
    }

    [[nodiscard]] inline uint8_t percent_to_raw(float percent) {
        if (!std::isfinite(percent)) {
            throw ValidationError("Angle value out of range [0, 100]");
        }
        const auto rounded = std::lround(static_cast<double>(percent) * kRawAngleMaximum / kAnglePercentMaximum);
        if (rounded < kRawAngleMinimum) {
            return kRawAngleMinimum;
        }
        if (rounded > kRawAngleMaximum) {
            return kRawAngleMaximum;
        }
        return static_cast<uint8_t>(rounded);
    }

    [[nodiscard]] inline float raw_to_percent(uint8_t raw) {
        return static_cast<float>(raw) * kAnglePercentMaximum / kRawAngleMaximum;
    }

    [[nodiscard]] inline std::array<uint8_t, kJointCount>
    percents_to_raw(const std::array<float, kJointCount> &values) {
        validate_angle_percents(values);
        std::array<uint8_t, kJointCount> raw{};
        for (size_t index = 0; index < kJointCount; ++index) {
            raw[index] = percent_to_raw(values[index]);
        }
        return raw;
    }

    [[nodiscard]] inline std::array<float, kJointCount> raw_to_percents(const std::array<uint8_t, kJointCount> &raw) {
        std::array<float, kJointCount> values{};
        for (size_t index = 0; index < kJointCount; ++index) {
            values[index] = raw_to_percent(raw[index]);
        }
        return values;
    }

} // namespace linkerbot::hand::o6
