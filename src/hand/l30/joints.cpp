#include "linkerbot/hand/l30/joints.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <linkerbot/hand/errors.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace linkerbot::hand::l30 {

    namespace {
        float range_of(const JointSpec &spec) {
            return static_cast<float>(spec.maximum) - static_cast<float>(spec.minimum);
        }
    } // namespace

    int16_t percent_to_raw(float percent, const JointSpec &spec) {
        const auto scaled = spec.reverse_percent ? static_cast<float>(spec.maximum) - range_of(spec) * percent / 100.0F
                                                 : static_cast<float>(spec.minimum) + range_of(spec) * percent / 100.0F;
        const auto rounded = std::lround(scaled);
        // A percent inside [0, 100] always lands inside the spec range, but
        // sensor-derived percentages can sit outside it, so clamp to int16
        // rather than to the joint limits.
        if (rounded < std::numeric_limits<int16_t>::min()) {
            return std::numeric_limits<int16_t>::min();
        }
        if (rounded > std::numeric_limits<int16_t>::max()) {
            return std::numeric_limits<int16_t>::max();
        }
        return static_cast<int16_t>(rounded);
    }

    float raw_to_percent(int16_t raw, const JointSpec &spec) {
        const auto offset = spec.reverse_percent ? static_cast<float>(spec.maximum) - static_cast<float>(raw)
                                                 : static_cast<float>(raw) - static_cast<float>(spec.minimum);
        return offset * 100.0F / range_of(spec);
    }

    std::array<int16_t, kJointCount> percents_to_raw(const std::array<float, kJointCount> &percents) {
        std::array<int16_t, kJointCount> raw{};
        for (size_t i = 0; i < kJointCount; ++i) {
            raw[i] = percent_to_raw(percents[i], kJointSpecs[i]);
        }
        return raw;
    }

    std::array<float, kJointCount> raw_to_percents(const std::array<int16_t, kJointCount> &raw) {
        std::array<float, kJointCount> percents{};
        for (size_t i = 0; i < kJointCount; ++i) {
            percents[i] = raw_to_percent(raw[i], kJointSpecs[i]);
        }
        return percents;
    }

    void validate_percents(const std::array<float, kJointCount> &percents, std::string_view label) {
        for (size_t i = 0; i < kJointCount; ++i) {
            if (!(percents[i] >= 0.0F) || !(percents[i] <= 100.0F)) {
                throw ValidationError(std::string(label) + "[" + std::to_string(i) +
                                      "] must be between 0 and 100, got " + std::to_string(percents[i]));
            }
        }
    }

    void validate_raw_commands(const std::array<int16_t, kJointCount> &raw, std::string_view label) {
        for (size_t i = 0; i < kJointCount; ++i) {
            const auto &spec = kJointSpecs[i];
            if (raw[i] < spec.minimum || raw[i] > spec.maximum) {
                throw ValidationError(std::string(label) + "[" + std::to_string(i) + "] (" + std::string(spec.name) +
                                      ") must be between " + std::to_string(spec.minimum) + " and " +
                                      std::to_string(spec.maximum) + ", got " + std::to_string(raw[i]));
            }
        }
    }

    void validate_u16_range(const std::array<uint16_t, kJointCount> &values, uint16_t minimum, uint16_t maximum,
                            std::string_view label) {
        for (size_t i = 0; i < kJointCount; ++i) {
            if (values[i] < minimum || values[i] > maximum) {
                throw ValidationError(std::string(label) + "[" + std::to_string(i) + "] must be between " +
                                      std::to_string(minimum) + " and " + std::to_string(maximum) + ", got " +
                                      std::to_string(values[i]));
            }
        }
    }

} // namespace linkerbot::hand::l30
