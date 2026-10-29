#pragma once

#include <cstdint>

#include <linkerbot/hand/api/config.hpp>

namespace linkerbot::hand::unified_detail {
    [[nodiscard]] const char *model_name(Model model) noexcept;
    [[nodiscard]] uint32_t classic_address(HandSide side);
    void validate_config(const HandDeviceConfig &config);
    [[nodiscard]] bool polling_supported(Model model, TelemetrySemantic semantic);
} // namespace linkerbot::hand::unified_detail
