#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <linkerbot/hand/protocol/hop.hpp>
#include <linkerbot/hand/protocol/transaction.hpp>
#include <optional>
#include <string>
#include <vector>

namespace linkerbot::hand::o6::hop {
    inline constexpr uint32_t kDefaultRequestId = 1;
    // Canonical order: thumb_flex, thumb_abd, index, middle, ring, pinky.
    // HOP single-byte slot layout: slot 5 = yaw_thumb (thumb_abd),
    // slots 10..14 = root1_thumb..root1_little.
    inline constexpr std::array<uint8_t, 6> kJointSlots{10, 5, 11, 12, 13, 14};
    inline constexpr uint8_t kFaultMask = 0xcf;
    inline constexpr size_t kMaxCan20Payload = 5;
    inline constexpr size_t kMaxCanFdPayload = hand::protocol::hop::kMaxHopPayload;

    inline constexpr uint8_t kMiPosition = 0x01;
    inline constexpr uint8_t kMiSpeed = 0x02;
    inline constexpr uint8_t kMiAcceleration = 0x03;
    inline constexpr uint8_t kMiCurrent = 0x04;
    inline constexpr uint8_t kMiTemperature = 0x07;
    inline constexpr uint8_t kMiStallTime = 0x09;
    inline constexpr uint8_t kMiStallThreshold = 0x0a;
    inline constexpr uint8_t kMiStallHoldCurrent = 0x0b;
    inline constexpr uint8_t kMiFault = 0x0c;
    inline constexpr uint8_t kMiSensorCommand = 0x31;
    inline constexpr uint8_t kMiSensorData1 = 0x32;
    inline constexpr uint8_t kMiSensorData2 = 0x33;
    inline constexpr uint8_t kMiSensorData3 = 0x34;
    inline constexpr uint8_t kMiConfig = 0x35;
    inline constexpr uint8_t kMiProductInfo = 0x41;

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
            throw ValidationError("O6 HOP request ID must be 0..0x3FE");
        }
        return id | 0x400U;
    }

    inline size_t max_payload_bytes(bool can_fd) noexcept { return can_fd ? kMaxCanFdPayload : kMaxCan20Payload; }

    inline can::Message build_message(uint32_t request_id, uint8_t mi, uint8_t si, uint8_t edl,
                                      const std::vector<uint8_t> &payload, bool rts, bool can_fd = true) {
        if (can_fd) {
            static_cast<void>(response_id(request_id));
            return hand::protocol::hop::build_message(request_id, mi, si, edl, payload, rts);
        }
        static_cast<void>(response_id(request_id));
        if (edl == 0 || edl > kMaxCan20Payload || static_cast<size_t>(si) + edl > 256) {
            throw ValidationError("O6 HOP CAN 2.0 range must contain 1..5 bytes within 256-byte object");
        }
        if (payload.size() > kMaxCan20Payload) {
            throw ValidationError("O6 HOP CAN 2.0 write payload exceeds 5 bytes");
        }
        if (!payload.empty() && payload.size() != edl) {
            throw ValidationError("O6 HOP CAN 2.0 write EDL does not match payload size");
        }
        can::Message msg{};
        msg.id = request_id;
        msg.is_extended = false;
        msg.is_fd = false;
        msg.is_brs = false;
        msg.dlc = static_cast<uint8_t>(hand::protocol::hop::kHeaderBytes + payload.size());
        msg.data[0] = hand::protocol::hop::ctrl_byte(mi, rts);
        msg.data[1] = si;
        msg.data[2] = edl;
        for (size_t i = 0; i < payload.size(); ++i) {
            msg.data[hand::protocol::hop::kHeaderBytes + i] = payload[i];
        }
        return msg;
    }

    // One bounded register range. 0x4F is history, never an implicit negative ACK.
    inline hand::protocol::TransactionPlan transaction(uint32_t id, uint8_t mi, uint8_t si, uint8_t length,
                                                       const std::vector<uint8_t> &payload = {}, bool rts = false,
                                                       std::string family = {}, bool can_fd = true) {
        const auto reply = response_id(id);
        const auto limit = max_payload_bytes(can_fd);
        if (!length || length > limit) {
            throw ValidationError(can_fd ? "O6 HOP range must contain 1..61 bytes"
                                         : "O6 HOP CAN 2.0 range must contain 1..5 bytes");
        }
        hand::protocol::TransactionPlan plan;
        plan.keys = {"hop"};
        plan.domain = "hop/range";
        plan.command_family = std::move(family);
        plan.publish_after_send = !payload.empty();
        plan.frames = {build_message(id, mi, si, length, payload, rts, can_fd)};
        plan.consume = [reply, mi, si, length, rts, can_fd](const can::Message &msg) {
            using hand::protocol::Progress;
            if (msg.id != reply || msg.is_extended || msg.is_fd != can_fd) {
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
            if (can_fd) {
                if (msg.dlc > 64 || hand::protocol::hop::canfd_wire_bytes(msg.dlc) != msg.dlc) {
                    throw ProtocolError("invalid O6 HOP CAN FD wire length");
                }
            } else if (msg.dlc > 8) {
                throw ProtocolError("invalid O6 HOP CAN 2.0 wire length");
            }
            const auto frame = hand::protocol::hop::parse_frame(msg.data.data(), msg.dlc);
            if (frame.edl != length) {
                throw ProtocolError("O6 HOP response EDL does not match requested range");
            }
            return Progress::complete(frame.payload);
        };
        return plan;
    }

    inline std::array<uint8_t, 6> project_bytes(const std::vector<uint8_t> &data) {
        if (data.size() != 10) {
            throw ProtocolError("O6 HOP joint range must contain 10 bytes");
        }
        std::array<uint8_t, 6> result{};
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] = data[kJointSlots[i] - 5];
        }
        return result;
    }

    // Both mounted ranges (SI=5 len=1 and SI=10 len=5) are one logical transaction:
    // one send receipt, and no successful value until both complete replies arrive
    // (in either order). Both frames fit within Classic CAN 2.0 (4 and 8 bytes)
    // as well as CAN FD.
    inline hand::protocol::TransactionPlan write_joints(uint32_t id, uint8_t mi, const std::vector<uint8_t> &canonical,
                                                        bool rts, std::string family, bool can_fd = true) {
        if (canonical.size() != 6) {
            throw ValidationError("O6 HOP requires six joint values");
        }
        const std::vector<uint8_t> thumb{canonical[1]};
        const std::vector<uint8_t> roots{canonical[0], canonical[2], canonical[3], canonical[4], canonical[5]};
        auto first = transaction(id, mi, 5, 1, thumb, rts, family, can_fd);
        auto second = transaction(id, mi, 10, 5, roots, rts, std::move(family), can_fd);
        first.frames.push_back(second.frames.front());
        first.domain = "hop/write";
        first.consume = [a = first.consume, b = second.consume,
                         parts =
                             std::array<std::optional<std::vector<uint8_t>>, 2>{}](const can::Message &msg) mutable {
            for (size_t i = 0; i < 2; ++i) {
                auto result = (i == 0 ? a : b)(msg);
                if (result.kind != hand::protocol::ProgressKind::complete) {
                    continue;
                }
                auto bytes = std::any_cast<std::vector<uint8_t>>(result.value);
                if (parts[i] && *parts[i] != bytes) {
                    throw ProtocolError("conflicting O6 HOP write replies");
                }
                parts[i] = std::move(bytes);
                if (!parts[0] || !parts[1]) {
                    return hand::protocol::Progress::pending();
                }
                std::vector<uint8_t> range(10);
                std::copy(parts[0]->begin(), parts[0]->end(), range.begin());
                std::copy(parts[1]->begin(), parts[1]->end(), range.begin() + 5);
                return hand::protocol::Progress::complete(std::move(range), hand::protocol::Origin::command_feedback);
            }
            return hand::protocol::Progress::unmatched();
        };
        return first;
    }

} // namespace linkerbot::hand::o6::hop
