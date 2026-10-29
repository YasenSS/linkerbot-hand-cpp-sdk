#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <linkerbot/hand/identity.hpp>

namespace linkerbot::hand::l30 {

    enum class HandSide { kLeft, kRight };

    [[nodiscard]] std::string_view hand_side_name(HandSide side);

    inline constexpr uint8_t kHandTypeLeft = 0;
    inline constexpr uint8_t kHandTypeRight = 1;

    struct Version {
        uint8_t major = 0;
        uint8_t minor = 0;
        uint8_t patch = 0;

        // Formatted as "V<major>.<minor>.<patch>".
        [[nodiscard]] std::string to_string() const;
    };

    // The 18-byte DeviceInfo structure of section 9.2.1.
    struct DeviceInfo {
        uint8_t product_id = 0; // 0x13 identifies an L30
        uint32_t serial_number = 0;
        Version software_version;
        Version hardware_version;
        Version structure_version;
        uint8_t node_id = 0;
        HandSide hand_side = HandSide::kLeft;
        // Z none, A Shanchuan, B Huaweike, J Jingzhi, F Fuerxin
        char sensor_type = '\0';
        char origin = '\0'; // A Beijing, B Dachang, C Gu'an, D Dongguan, ...
        std::chrono::steady_clock::time_point timestamp;
    };

    // Shared pure identity codec used by VersionManager, HandDevice and
    // discovery probes. The response includes the L30 framing bytes.
    [[nodiscard]] DeviceInfo decode_device_info_response(const std::vector<uint8_t> &response);
    void verify_device_info(const DeviceInfo &info, uint8_t expected_node_id, std::optional<IdentitySide> expected_side,
                            std::string_view product_code);

} // namespace linkerbot::hand::l30
