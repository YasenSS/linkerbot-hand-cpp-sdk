#pragma once
#include <linkerbot/hand/protocol/hop.hpp>
#include <linkerbot/hand/protocol/transaction.hpp>

namespace linkerbot::hand::l6::hop {
    inline constexpr uint32_t kDefaultRequestId = 1;
    inline constexpr std::array<uint8_t, 6> kJointSlots{10, 5, 11, 12, 13, 14};
    inline constexpr uint8_t kFaultMask = 0xcf;
    // MI 0x4F history entries are bitmasks, unrelated to O30 immediate errors.
    enum class CommunicationError : uint8_t {
        kNone = 0,
        kMainIndexMissing = 0x01,
        kSubIndexMissing = 0x02,
        kNotWritable = 0x04,
        kLengthMismatch = 0x08,
        kPermissionDenied = 0x10,
        kZeroPaddedResponse = 0x20
    };
    inline uint32_t response_id(uint32_t id) {
        if (id > 0x3fe) {
            throw ValidationError("L6 HOP request ID must be 0..0x3FE");
        }
        return id | 0x400;
    }
    // One bounded register range. 0x4F is history, never an implicit negative ACK.
    inline hand::protocol::TransactionPlan transaction(uint32_t id, uint8_t mi, uint8_t si, uint8_t length,
                                                       const std::vector<uint8_t> &payload = {}, bool rts = false,
                                                       std::string family = {}) {
        const auto reply = response_id(id);
        if (!length || length > hand::protocol::hop::kMaxHopPayload) {
            throw ValidationError("L6 HOP range must contain 1..61 bytes");
        }
        hand::protocol::TransactionPlan plan;
        plan.keys = {"hop"};
        plan.domain = "hop/range";
        plan.command_family = std::move(family);
        plan.publish_after_send = !payload.empty();
        plan.frames = {hand::protocol::hop::build_message(id, mi, si, length, payload, rts)};
        plan.consume = [reply, mi, si, length, rts](const can::Message &msg) {
            using hand::protocol::Progress;
            if (msg.id != reply || msg.is_extended || !msg.is_fd) {
                return Progress::unmatched();
            }
            if (msg.dlc && msg.data[0] != hand::protocol::hop::ctrl_byte(mi, rts)) {
                return Progress::unmatched();
            }
            // A different range is unrelated (e.g. a duplicate from the previous
            // chunk); a malformed response for this range fails the transaction.
            if (msg.dlc >= 2 && msg.data[1] != si) {
                return Progress::unmatched();
            }
            if (msg.dlc > 64 || hand::protocol::hop::canfd_wire_bytes(msg.dlc) != msg.dlc) {
                throw ProtocolError("invalid L6 HOP CAN FD wire length");
            }
            const auto frame = hand::protocol::hop::parse_frame(msg.data.data(), msg.dlc);
            if (frame.edl != length) {
                throw ProtocolError("L6 HOP response EDL does not match requested range");
            }
            return Progress::complete(frame.payload);
        };
        return plan;
    }
    inline std::array<uint16_t, 6> project_positions(const std::vector<uint8_t> &data) {
        if (data.size() != 20) {
            throw ProtocolError("L6 HOP position range must contain 20 bytes");
        }
        std::array<uint16_t, 6> result{};
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] = hand::protocol::hop::read_u16_le(data.data() + 2 * (kJointSlots[i] - 5));
        }
        return result;
    }
    inline std::array<uint8_t, 6> project_bytes(const std::vector<uint8_t> &data) {
        if (data.size() != 10) {
            throw ProtocolError("L6 HOP joint range must contain 10 bytes");
        }
        std::array<uint8_t, 6> result{};
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] = data[kJointSlots[i] - 5];
        }
        return result;
    }
    // Both mounted ranges are one logical transaction: one send receipt, and no
    // successful value until both complete replies arrive (in either order).
    inline hand::protocol::TransactionPlan write_joints(uint32_t id, uint8_t mi, const std::vector<uint8_t> &canonical,
                                                        bool halfword, bool rts, std::string family) {
        const size_t width = halfword ? 2 : 1;
        if (canonical.size() != 6 * width) {
            throw ValidationError("L6 HOP requires six joint values");
        }
        std::vector<uint8_t> thumb(canonical.begin() + width, canonical.begin() + 2 * width);
        std::vector<uint8_t> roots(canonical.begin(), canonical.begin() + width);
        roots.insert(roots.end(), canonical.begin() + 2 * width, canonical.end());
        auto first = transaction(id, mi, 5 * width, width, thumb, rts, family);
        auto second = transaction(id, mi, 10 * width, 5 * width, roots, rts, family);
        first.frames.push_back(second.frames.front());
        first.domain = "hop/write";
        first.consume = [a = first.consume, b = second.consume, width,
                         parts =
                             std::array<std::optional<std::vector<uint8_t>>, 2>{}](const can::Message &msg) mutable {
            for (size_t i = 0; i < 2; ++i) {
                auto result = (i == 0 ? a : b)(msg);
                if (result.kind != hand::protocol::ProgressKind::complete) {
                    continue;
                }
                auto bytes = std::any_cast<std::vector<uint8_t>>(result.value);
                if (parts[i] && *parts[i] != bytes) {
                    throw ProtocolError("conflicting L6 HOP write replies");
                }
                parts[i] = std::move(bytes);
                if (!parts[0] || !parts[1]) {
                    return hand::protocol::Progress::pending();
                }
                std::vector<uint8_t> range(10 * width);
                std::copy(parts[0]->begin(), parts[0]->end(), range.begin());
                std::copy(parts[1]->begin(), parts[1]->end(), range.begin() + 5 * width);
                return hand::protocol::Progress::complete(std::move(range), hand::protocol::Origin::command_feedback);
            }
            return hand::protocol::Progress::unmatched();
        };
        return first;
    }

}
