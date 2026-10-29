#include <linkerbot/hand/errors.hpp>
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace linkerbot::hand::l20 {

    constexpr size_t kJointCount = 16;

    // Canonical 16-joint order used by every domain (angle/speed/current/fault
    // /temperature). Public names match the unified Python SDK; wire order
    // and legacy typed member names remain unchanged.
    constexpr std::array<std::string_view, kJointCount> kJointNames = {
        "thumb_rotation", "thumb_roll", "thumb_root",  "thumb_tip",  "index_abd", "index_root",
        "index_tip",      "middle_abd", "middle_root", "middle_tip", "ring_abd",  "ring_root",
        "ring_tip",       "pinky_abd",  "pinky_root",  "pinky_tip"};

    inline uint8_t percent_to_raw(float percent) {
        const auto rounded = std::lround(static_cast<double>(percent) * 255.0 / 100.0);
        if (rounded < 0) {
            return 0;
        }
        if (rounded > 255) {
            return 255;
        }
        return static_cast<uint8_t>(rounded);
    }

    inline float raw_to_percent(uint8_t raw) { return static_cast<float>(raw) * 100.0f / 255.0f; }

    inline void validate_percents(const std::array<float, kJointCount> &values, const char *label) {
        for (auto v : values) {
            if (std::isnan(v) || v < 0.0f || v > 100.0f) {
                throw ValidationError(std::string(label) + " value out of range [0, 100]");
            }
        }
    }

    inline std::array<uint8_t, kJointCount> percents_to_raw(const std::array<float, kJointCount> &values) {
        std::array<uint8_t, kJointCount> raw{};
        for (size_t i = 0; i < kJointCount; ++i) {
            raw[i] = percent_to_raw(values[i]);
        }
        return raw;
    }

    inline std::array<float, kJointCount> raw_to_percents(const std::array<uint8_t, kJointCount> &raw) {
        std::array<float, kJointCount> values{};
        for (size_t i = 0; i < kJointCount; ++i) {
            values[i] = raw_to_percent(raw[i]);
        }
        return values;
    }

} // namespace linkerbot::hand::l20
