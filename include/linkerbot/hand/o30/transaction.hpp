#pragma once
#include <algorithm>
#include <linkerbot/hand/o30/protocol.hpp>
#include <linkerbot/hand/protocol/transaction.hpp>
#include <optional>
#include <string>
#include <vector>

namespace linkerbot::hand::o30::protocol {
    inline const char *write_command_family(uint8_t mi) {
        switch (mi) {
        case kMiPosition:
        case kMiPosition16:
        case kMiSparsePosition:
            return "angle_targets";
        case kMiSpeed:
            return "speed_targets";
        case kMiCurrent:
            return "current_limits";
        case kMiAcceleration:
            return "acceleration_targets";
        case kMiStallTime:
        case kMiStallThreshold:
        case kMiStallHoldCurrent:
            return "stall";
        case kMiMapping:
        case kMiPositionPidP:
        case kMiPositionPidI:
        case kMiPositionPidD:
        case kMiSpeedPidP:
        case kMiSpeedPidI:
        case kMiSpeedPidD:
        case kMiConfig:
        case kMiPreset1:
        case kMiPreset2:
        case kMiPreset3:
        case kMiPreset4:
        case kMiPreset5:
        case kMiService:
        case kMiUnit:
            return "configuration";
        default:
            return "hop_writes";
        }
    }

    // One instance belongs to exactly one on-wire operation. It has no I/O,
    // clocks, locks, global cache, or external lifecycle references.
    class RangeResponse {
        struct ConstructorTag {};
        struct ConstructorArguments {
            uint32_t request_id;
            uint8_t mi;
            uint8_t si;
            uint8_t length;
            bool rts;
            bool write_ack;
            std::optional<uint32_t> next_id;
        };
        RangeResponse(ConstructorTag, ConstructorArguments arguments)
            : response_id_(expected_response_id(arguments.request_id)), next_id_(arguments.next_id), mi_(arguments.mi),
              si_(arguments.si), length_(arguments.length), rts_(arguments.rts), write_ack_(arguments.write_ack),
              data_(arguments.length), covered_(arguments.length, false) {}

    public:
        RangeResponse(uint32_t request_id, uint8_t mi, uint8_t si, uint8_t length, bool rts, bool write_ack,
                      std::optional<uint32_t> next_id = {})
            : RangeResponse(ConstructorTag{},
                            ConstructorArguments{request_id, mi, si, length, rts, write_ack, next_id}) {}
        hand::protocol::Progress consume(const can::Message &message) {
            using Progress = hand::protocol::Progress;
            if (message.is_extended || !message.is_fd || message.dlc > can::kMaxDataLen ||
                (message.id != response_id_ && (!next_id_ || message.id != expected_response_id(*next_id_)))) {
                return Progress::unmatched();
            }
            if (message.dlc >= 1) {
                const auto mi = mi_of(message.data[0]);
                if (mi != mi_ && mi != kMiCommError) {
                    return Progress::unmatched();
                }
                if (mi == mi_ && rts_of(message.data[0]) != rts_) {
                    return Progress::unmatched();
                }
            }
            const auto frame = parse_frame(message.data.data(), message.dlc);
            if (is_error_response(frame, mi_)) {
                const auto code = error_code_of(frame);
                throw ProtocolError("HOP error " + hex_byte(code) + " (" + std::string(error_message(code)) +
                                        ") at SI " + hex_byte(frame.si),
                                    code);
            }
            if (frame.mi != mi_ || frame.rts != rts_) {
                return Progress::unmatched();
            }
            if (frame.si < si_) {
                throw ProtocolError("HOP fragment precedes requested SI");
            }
            const auto offset = static_cast<size_t>(frame.si - si_);
            const auto end = offset + frame.payload.size();
            if (frame.payload.empty() || end > length_) {
                throw ProtocolError("HOP fragment is empty or outside requested range");
            }
            for (size_t i = 0; i < frame.payload.size(); ++i) {
                if (covered_[offset + i] && data_[offset + i] != frame.payload[i]) {
                    throw ProtocolError("conflicting overlapping HOP fragments");
                }
                covered_[offset + i] = true;
                data_[offset + i] = frame.payload[i];
            }
            // The deployed error-history object explicitly permits 15 or 16 bytes.
            // Every other read is exact; a terminal short object is a protocol
            // failure, never a successful partially-filled sample.
            if (!write_ack_ && frame.edl < kMaxHopPayload && end < length_) {
                if (mi_ == kMiCommError && si_ == 1 && length_ == 16 && offset == 0 && end == 15) {
                    // Progress owns this payload through std::any; covered by ownership regression tests.
                    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
                    return Progress::complete(std::vector<uint8_t>(data_.begin(), data_.begin() + 15));
                }
                throw ProtocolError("short HOP object does not cover requested range");
            }
            if (write_ack_ && (frame.edl < kMaxHopPayload || end == length_)) {
                terminal_ = end;
            }
            const auto required = write_ack_ ? terminal_.value_or(length_) : static_cast<size_t>(length_);
            if ((!write_ack_ || terminal_) &&
                std::all_of(covered_.begin(), covered_.begin() + static_cast<std::ptrdiff_t>(required),
                            [](bool covered) { return covered; })) {
                // Progress owns this payload through std::any; covered by ownership regression tests.
                // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
                return Progress::complete(
                    std::vector<uint8_t>(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(required)));
            }
            return Progress::pending();
        }

    private:
        uint32_t response_id_;
        std::optional<uint32_t> next_id_;
        uint8_t mi_, si_, length_;
        bool rts_, write_ack_;
        std::vector<uint8_t> data_;
        std::vector<bool> covered_;
        std::optional<size_t> terminal_;
    };
    inline hand::protocol::TransactionPlan transaction(uint32_t id, uint8_t mi, uint8_t si, uint8_t length,
                                                       const std::vector<uint8_t> &payload, bool rts,
                                                       std::optional<uint32_t> next_id = {}) {
        hand::protocol::TransactionPlan plan;
        plan.keys = {"hop"}; // CommError replies do not identify every subrange.
        plan.domain = "hop/" + std::to_string(mi) + "/" + std::to_string(si) + (rts ? "/set" : "/measured");
        if (!payload.empty()) {
            plan.command_family = write_command_family(mi);
        }
        plan.frames = {build_message(id, mi, si, length, payload, rts)};
        plan.consume = [assembler = RangeResponse(id, mi, si, length, rts, !payload.empty(), next_id)](
                           const can::Message &message) mutable {
            try {
                return assembler.consume(message);
            } catch (const ProtocolError &error) {
                auto progress = hand::protocol::Progress::pending();
                progress.kind = hand::protocol::ProgressKind::failed;
                progress.error = std::current_exception();
                // A decoded negative ACK completes the wire transaction. A
                // malformed/incomplete object has no such completion evidence.
                progress.terminal_error = error.error_code() != 0;
                return progress;
            }
        };
        return plan;
    }
} // namespace linkerbot::hand::o30::protocol
