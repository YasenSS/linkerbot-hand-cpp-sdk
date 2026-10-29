#pragma once

#include <optional>
#include <vector>

#include <linkerbot/hand/capabilities.hpp>

namespace linkerbot::hand {
    [[nodiscard]] std::vector<double> validate_command_values(const std::vector<double> &values,
                                                              const CommandCapability &capability);

    [[nodiscard]] std::vector<double> percent_to_raw_values(const std::vector<double> &values,
                                                            const CommandCapability &capability);

    [[nodiscard]] std::vector<double> validate_raw_angles(const std::vector<double> &values,
                                                          const DeviceCapabilities &capabilities);

    [[nodiscard]] std::vector<double> percent_to_raw_angles(const std::vector<double> &values,
                                                            const DeviceCapabilities &capabilities);

    // Feedback conversion deliberately does not clamp out-of-range evidence.
    [[nodiscard]] std::vector<std::optional<double>> raw_angles_to_percent(const std::vector<double> &values,
                                                                           const DeviceCapabilities &capabilities);

    [[nodiscard]] std::vector<std::optional<double>> raw_angles_to_physical_rad(const std::vector<double> &values,
                                                                                const DeviceCapabilities &capabilities);

    [[nodiscard]] bool raw_angles_within_declared_range(const std::vector<double> &values,
                                                        const DeviceCapabilities &capabilities);
} // namespace linkerbot::hand
