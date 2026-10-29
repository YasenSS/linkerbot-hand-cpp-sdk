#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace linkerbot::hand::l20lite {

    constexpr uint8_t kUnknownDirection = 255;

    struct TipFinger {
        uint8_t normal = 0;
        uint8_t tangential = 0;
        uint8_t direction_raw = kUnknownDirection;
        uint8_t proximity = 0;

        [[nodiscard]] std::optional<float> direction_deg() const {
            if (direction_raw > 127) {
                return std::nullopt;
            }
            return static_cast<float>(direction_raw) * 359.0f / 127.0f;
        }
    };

    struct TipForce {
        static constexpr size_t kFingerCount = 5;

        TipFinger thumb;
        TipFinger index;
        TipFinger middle;
        TipFinger ring;
        TipFinger pinky;

        [[nodiscard]] std::array<TipFinger, kFingerCount> to_array() const {
            return {thumb, index, middle, ring, pinky};
        }

        [[nodiscard]] static TipForce from_array(const std::array<TipFinger, kFingerCount> &values) {
            return {values[0], values[1], values[2], values[3], values[4]};
        }
    };

    struct TipForceData {
        TipForce tip_force;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l20lite
