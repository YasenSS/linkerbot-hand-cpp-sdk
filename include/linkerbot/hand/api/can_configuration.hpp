#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <linkerbot/hand/errors.hpp>

namespace linkerbot::hand {
    // Device-side HOP MI 0x35 CAN controller settings. These values do not
    // reconfigure the host adapter.
    enum class HopCanInterfaceMode : uint8_t { kCan20 = 1, kCanFd = 2 };

    // Nominal bitrate codes 1..8 from MI 0x35 SI 0x08.
    inline constexpr std::array<uint32_t, 8> kHopNominalBitrates{1000000, 800000, 500000, 400000,
                                                                 250000,  200000, 125000, 100000};

    // Data bitrate codes 1..13 from MI 0x35 SI 0x09.
    inline constexpr std::array<uint32_t, 13> kHopDataBitrates{
        8000000, 5000000, 4000000, 3000000, 2000000, 1000000, 800000, 500000, 400000, 250000, 200000, 125000, 100000};

    struct HopCanConfiguration {
        uint16_t request_id = 0;
        HopCanInterfaceMode interface_mode = HopCanInterfaceMode::kCanFd;
        bool brs = false;
        uint32_t nominal_bitrate = 0;
        uint32_t data_bitrate = 0;

        [[nodiscard]] uint32_t response_id() const noexcept { return static_cast<uint32_t>(request_id) | 0x400U; }
    };

    // Outcome of one interface-mode change. A mode write is sent at most once.
    // saved_to_flash is true after a single accepted 1-byte completion for
    // MI 0x35 SI 0x1E. Real O6/O30 firmware completes Flash save with 0x00;
    // existing fixtures echo the request byte 0x01. Both completions are accepted.
    // active_format_changed is true when the reply wire format already matches
    // the new mode. False means the register was verified and a reboot is still
    // required before the active format changes.
    struct HopCanModeSwitchResult {
        HopCanConfiguration before;
        HopCanConfiguration after;
        bool write_sent = false;
        bool saved_to_flash = false;
        bool active_wire_is_fd = true;
        bool active_format_changed = false;
    };

    [[nodiscard]] inline bool is_hop_can_interface_mode(HopCanInterfaceMode mode) noexcept {
        return mode == HopCanInterfaceMode::kCan20 || mode == HopCanInterfaceMode::kCanFd;
    }

    [[nodiscard]] inline uint32_t decode_hop_nominal_bitrate(uint8_t code) {
        if (code < 1 || code > kHopNominalBitrates.size()) {
            throw ProtocolError("HOP returned invalid CAN configuration codes");
        }
        return kHopNominalBitrates[static_cast<size_t>(code - 1)];
    }

    [[nodiscard]] inline uint32_t decode_hop_data_bitrate(uint8_t code) {
        if (code < 1 || code > kHopDataBitrates.size()) {
            throw ProtocolError("HOP returned invalid CAN configuration codes");
        }
        return kHopDataBitrates[static_cast<size_t>(code - 1)];
    }

    // SI 0x00 is a little-endian request ID. SI 0x06 is interface mode, BRS,
    // nominal bitrate code and data bitrate code.
    [[nodiscard]] inline HopCanConfiguration parse_hop_can_configuration(const uint8_t *request_id,
                                                                         size_t request_id_size,
                                                                         const uint8_t *interface_bytes,
                                                                         size_t interface_size) {
        if (request_id == nullptr || interface_bytes == nullptr || request_id_size != 2 || interface_size != 4) {
            throw ProtocolError("HOP CAN configuration requires a 2-byte request ID and a 4-byte interface block");
        }
        const auto id =
            static_cast<uint16_t>(static_cast<uint16_t>(request_id[0]) | (static_cast<uint16_t>(request_id[1]) << 8U));
        if (id > 0x3FE) {
            throw ProtocolError("HOP returned invalid standard CAN ID");
        }
        if (interface_bytes[0] != static_cast<uint8_t>(HopCanInterfaceMode::kCan20) &&
            interface_bytes[0] != static_cast<uint8_t>(HopCanInterfaceMode::kCanFd)) {
            throw ProtocolError("HOP returned invalid CAN configuration codes");
        }
        if (interface_bytes[1] > 1) {
            throw ProtocolError("HOP returned invalid CAN configuration codes");
        }
        HopCanConfiguration result;
        result.request_id = id;
        result.interface_mode = static_cast<HopCanInterfaceMode>(interface_bytes[0]);
        result.brs = interface_bytes[1] == 1;
        result.nominal_bitrate = decode_hop_nominal_bitrate(interface_bytes[2]);
        result.data_bitrate = decode_hop_data_bitrate(interface_bytes[3]);
        return result;
    }
} // namespace linkerbot::hand
