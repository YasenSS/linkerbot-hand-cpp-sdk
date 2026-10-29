#include <linkerbot/hand/errors.hpp>
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace linkerbot::hand::l20lite {

    constexpr size_t kJointCount = 10;

    constexpr std::array<std::string_view, kJointCount> kJointNames = {
        "thumb_flex", "thumb_roll", "index_flex", "middle_flex", "ring_flex",
        "pinky_flex", "index_abd",  "ring_abd",   "pinky_abd",   "thumb_rotation"};

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

    // New protocol 0x87/0x88: range 0–0xFE, linear 0–100% ↔ 0–254.
    inline uint8_t percent_to_accel_raw(float percent) {
        const auto rounded = std::lround(static_cast<double>(percent) * 254.0 / 100.0);
        if (rounded < 0) {
            return 0;
        }
        if (rounded > 254) {
            return 254;
        }
        return static_cast<uint8_t>(rounded);
    }

    inline float accel_raw_to_percent(uint8_t raw) {
        const uint8_t clamped = raw > 254 ? uint8_t{254} : raw;
        return static_cast<float>(clamped) * 100.0f / 254.0f;
    }

    inline std::array<uint8_t, kJointCount> percents_to_accel_raw(const std::array<float, kJointCount> &values) {
        std::array<uint8_t, kJointCount> raw{};
        for (size_t i = 0; i < kJointCount; ++i) {
            raw[i] = percent_to_accel_raw(values[i]);
        }
        return raw;
    }

    inline std::array<float, kJointCount> accel_raw_to_percents(const std::array<uint8_t, kJointCount> &raw) {
        std::array<float, kJointCount> values{};
        for (size_t i = 0; i < kJointCount; ++i) {
            values[i] = accel_raw_to_percent(raw[i]);
        }
        return values;
    }

} // namespace linkerbot::hand::l20lite
