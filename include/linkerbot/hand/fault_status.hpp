#pragma once

#include <cstdint>

namespace linkerbot::hand {
    // Preserve the wire byte, distinguish documented flags from reserved bits.
    // A defined bit is a protocol interpretation, not proof of hardware validation.
    struct FaultStatus {
        uint8_t raw = 0;
        uint8_t known_bits = 0;
        uint8_t reserved_bits = 0;

        [[nodiscard]] constexpr bool has_any_fault() const { return known_bits != 0; }

        [[nodiscard]] constexpr bool has_any_bit() const { return raw != 0; }
    };

    [[nodiscard]] constexpr FaultStatus decode_fault_status(uint8_t raw, uint8_t defined_mask) {
        return {raw, static_cast<uint8_t>(raw & defined_mask),
                static_cast<uint8_t>(raw & static_cast<uint8_t>(~defined_mask))};
    }
}
