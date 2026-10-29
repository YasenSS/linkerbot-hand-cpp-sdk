#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <linkerbot/hand/identity.hpp>

namespace linkerbot::hand::o30 {

    // MI 0x41: deployed 225-byte or documented 249-byte product-info layout.
    struct ProductInfo {
        std::string model;
        std::string voltage_range;
        std::string mcu_uid;
        std::string device_uid;
        std::string protocol_name;
        std::string protocol_version;
        std::string interface_hw_version;
        std::string adapter_hw_version;
        std::string control_hw_version;
        std::string bootloader_version;
        std::string app_version;
        std::string mechanical_version;
        std::string build_time;
        std::string supported_interfaces;
        std::string hand_side;
        std::chrono::steady_clock::time_point timestamp;
    };

    // Shared pure identity codec/validation used by VersionManager,
    // HandDevice and discovery probes.
    [[nodiscard]] bool product_info_uses_documented_layout(const std::vector<uint8_t> &deployed_blob);
    [[nodiscard]] ProductInfo decode_product_info_blob(const std::vector<uint8_t> &blob);
    void verify_product_info(const ProductInfo &info, std::optional<IdentitySide> expected_side = {});

} // namespace linkerbot::hand::o30
