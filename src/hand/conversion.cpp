#include <linkerbot/hand/conversion.hpp>

#include <cmath>
#include <string>

#include <linkerbot/hand/errors.hpp>

namespace linkerbot::hand {
    namespace {
        void require_values(const std::vector<double> &values, size_t expected, const char *label) {
            if (values.size() != expected) {
                throw ValidationError(std::string(label) + " requires " + std::to_string(expected) + " values, got " +
                                      std::to_string(values.size()));
            }
            for (const auto value : values) {
                if (!std::isfinite(value)) {
                    throw ValidationError(std::string(label) + " values must be finite");
                }
            }
        }

        [[nodiscard]] double quantize(double value, Quantization rule) {
            switch (rule) {
            case Quantization::kNearestTiesAwayFromZero:
                return value >= 0.0 ? std::floor(value + 0.5) : std::ceil(value - 0.5);
            case Quantization::kFloor:
                return std::floor(value);
            case Quantization::kCeil:
                return std::ceil(value);
            }
            throw ValidationError("unsupported quantization rule");
        }
    } // namespace

    std::vector<double> validate_command_values(const std::vector<double> &values,
                                                const CommandCapability &capability) {
        require_values(values, capability.channels.size(), "command values");
        for (size_t index = 0; index < values.size(); ++index) {
            const auto *range = capability.range_for(index);
            if (!range || !range->contains(values[index])) {
                throw ValidationError("command value at index " + std::to_string(index) +
                                      " is outside the declared range");
            }
        }
        return values;
    }

    std::vector<double> percent_to_raw_values(const std::vector<double> &values, const CommandCapability &capability) {
        require_values(values, capability.channels.size(), "percent values");
        if (capability.percent_mappings.size() != capability.channels.size()) {
            throw UnsupportedOperationError("command does not declare percent mappings");
        }

        std::vector<double> converted;
        converted.reserve(values.size());
        for (size_t index = 0; index < values.size(); ++index) {
            const auto percent = values[index];
            if (percent < 0.0 || percent > 100.0) {
                throw ValidationError("percent value at index " + std::to_string(index) + " must be within 0..100");
            }
            const auto &mapping = capability.percent_mappings[index];
            if (!mapping) {
                throw UnsupportedOperationError("command channel has no percent mapping");
            }
            auto raw = mapping->raw_at_zero + percent / 100.0 * (mapping->raw_at_hundred - mapping->raw_at_zero);
            const auto *range = capability.range_for(index);
            if (!range) {
                throw ValidationError("command range metadata is invalid");
            }
            if (range->integer_only) {
                raw = quantize(raw, mapping->quantization);
            }
            if (!range->contains(raw)) {
                throw ValidationError("percent mapping produced an out-of-range value");
            }
            converted.push_back(raw);
        }
        return converted;
    }

    std::vector<double> validate_raw_angles(const std::vector<double> &values, const DeviceCapabilities &capabilities) {
        const auto *command = capabilities.command(CommandSemantic::kAngles);
        if (!command) {
            throw UnsupportedOperationError("angles are not writable");
        }
        return validate_command_values(values, *command);
    }

    std::vector<double> percent_to_raw_angles(const std::vector<double> &values,
                                              const DeviceCapabilities &capabilities) {
        const auto *command = capabilities.command(CommandSemantic::kAngles);
        if (!command) {
            throw UnsupportedOperationError("angles are not writable");
        }
        return percent_to_raw_values(values, *command);
    }

    std::vector<std::optional<double>> raw_angles_to_percent(const std::vector<double> &values,
                                                             const DeviceCapabilities &capabilities) {
        require_values(values, capabilities.joints.size(), "raw angles");
        std::vector<std::optional<double>> converted;
        converted.reserve(values.size());
        for (size_t index = 0; index < values.size(); ++index) {
            const auto &mapping = capabilities.joints[index].percent_mapping;
            if (!mapping) {
                converted.push_back(std::nullopt);
                continue;
            }
            converted.push_back(100.0 * (values[index] - mapping->raw_at_zero) /
                                (mapping->raw_at_hundred - mapping->raw_at_zero));
        }
        return converted;
    }

    std::vector<std::optional<double>> raw_angles_to_physical_rad(const std::vector<double> &values,
                                                                  const DeviceCapabilities &capabilities) {
        require_values(values, capabilities.joints.size(), "raw angles");
        std::vector<std::optional<double>> converted;
        converted.reserve(values.size());
        for (size_t index = 0; index < values.size(); ++index) {
            const auto &mapping = capabilities.joints[index].physical_angle_mapping;
            if (!mapping) {
                converted.push_back(std::nullopt);
                continue;
            }
            if (mapping->raw_at_first == mapping->raw_at_second) {
                throw ValidationError("physical angle mapping endpoints must differ");
            }
            converted.push_back(mapping->radians_at_first +
                                (values[index] - mapping->raw_at_first) *
                                    (mapping->radians_at_second - mapping->radians_at_first) /
                                    (mapping->raw_at_second - mapping->raw_at_first));
        }
        return converted;
    }

    bool raw_angles_within_declared_range(const std::vector<double> &values, const DeviceCapabilities &capabilities) {
        require_values(values, capabilities.joints.size(), "raw angles");
        for (size_t index = 0; index < values.size(); ++index) {
            if (!capabilities.joints[index].raw_angle_range.contains(values[index])) {
                return false;
            }
        }
        return true;
    }
} // namespace linkerbot::hand
