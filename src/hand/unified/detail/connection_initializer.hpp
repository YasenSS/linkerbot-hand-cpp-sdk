#pragma once

#include "backend.hpp"

#include <string_view>
#include <vector>

#include <linkerbot/hand/api/config.hpp>
#include <linkerbot/hand/capabilities.hpp>
#include <linkerbot/hand/identity.hpp>

namespace linkerbot::hand::unified_detail {
    struct ConnectionInitializationResult {
        DeviceIdentity identity;
        DeviceCapabilities capabilities;
    };

    [[nodiscard]] std::vector<TelemetrySemantic> baseline_semantics(Model model);

    [[nodiscard]] DeviceCapabilities capabilities_for_identity(const DeviceIdentity &identity,
                                                               const HandDeviceConfig *config = nullptr);

    void validate_connection_identity(const DeviceIdentity &identity, const HandDeviceConfig &config,
                                      const DeviceIdentity *expected_identity = nullptr,
                                      std::string_view operation = "open");

    [[nodiscard]] ConnectionInitializationResult
    initialize_connection(DeviceBackend &backend, const HandDeviceConfig &config, const DeviceCapabilities &declared,
                          const DeviceIdentity *expected_identity = nullptr, std::string_view operation = "open");
} // namespace linkerbot::hand::unified_detail
