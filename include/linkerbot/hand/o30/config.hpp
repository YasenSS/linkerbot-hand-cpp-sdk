#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    // MI 0x35, 31-byte configuration block. Named accessors cover the fields
    // the protocol has assigned SI ranges to; the rest stay in `raw`.
    //
    // Continuous multi-field writes currently fail with error 0x02. Use the
    // single-field setters on ConfigManager.
    struct DeviceConfig {
        std::array<uint8_t, protocol::kConfigBytes> raw{};
        std::chrono::steady_clock::time_point timestamp;

        [[nodiscard]] uint16_t standard_id() const;
        [[nodiscard]] uint32_t extended_id() const;
        [[nodiscard]] uint8_t can_type() const;
        [[nodiscard]] uint8_t brs() const;
        [[nodiscard]] uint8_t control_mode() const;
        [[nodiscard]] uint8_t led() const;
        [[nodiscard]] uint8_t buzzer() const;
    };

} // namespace linkerbot::hand::o30
