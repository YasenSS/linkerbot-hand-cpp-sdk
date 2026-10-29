#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30::detail {

    [[nodiscard]] inline bool rts_of(ValueKind kind) { return kind == ValueKind::kSetpoint; }

    [[nodiscard]] inline std::array<uint8_t, kSlotCount> read_slots(Client &client, uint8_t mi, ValueKind kind,
                                                                    std::chrono::milliseconds timeout) {
        protocol::require_positive_timeout(timeout);
        return protocol::as_u8_slots(client.read(mi, 0, protocol::kU8ObjectBytes, rts_of(kind), timeout));
    }

    [[nodiscard]] inline std::array<uint8_t, kJointCount> read_joints(Client &client, uint8_t mi, ValueKind kind,
                                                                      std::chrono::milliseconds timeout) {
        return project_u8(read_slots(client, mi, kind, timeout));
    }

    inline void write_joints(Client &client, uint8_t mi, const std::array<uint8_t, kJointCount> &joints,
                             std::chrono::milliseconds timeout) {
        protocol::require_positive_timeout(timeout);
        client.write_mounted(mi, expand_u8(joints), timeout);
    }

    inline void write_all(Client &client, uint8_t mi, uint8_t value, std::chrono::milliseconds timeout) {
        std::array<uint8_t, kJointCount> joints{};
        joints.fill(value);
        write_joints(client, mi, joints, timeout);
    }

} // namespace linkerbot::hand::o30::detail
