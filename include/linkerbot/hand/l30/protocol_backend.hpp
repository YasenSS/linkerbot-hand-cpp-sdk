#pragma once
#include <linkerbot/hand/l30/protocol.hpp>
#include <linkerbot/hand/protocol/transaction.hpp>
#include <optional>

namespace linkerbot::hand::l30::protocol {
    namespace common = ::linkerbot::hand::protocol;
    inline can::Message request_frame(uint8_t node, uint8_t host, uint8_t parent, uint8_t subcmd, uint8_t access,
                                      const std::vector<uint8_t> &payload, uint8_t bytes) {
        return build_message(FrameId{kPriorityControl, access, parent, subcmd, node, host}, payload, bytes);
    }
    inline bool valid_frame(const can::Message &m, uint32_t id) {
        return m.id == id && m.is_extended && m.is_fd && m.dlc <= m.data.size();
    }
    inline std::string response_key(uint32_t id) { return "l30.response." + std::to_string(id); }
    inline const char *write_command_family(uint8_t parent, uint8_t subcmd) {
        if (parent == kParentControl) {
            switch (subcmd) {
            case kSubPosition:
                return "angle_targets";
            case kSubCurrentLimit:
                return "current_limits";
            case kSubSpeed:
                return "speed_targets";
            case kSubFault:
                return "fault_reset";
            default:
                return "configuration";
            }
        }
        if (parent == kParentConfig || parent == kParentPeriodic || parent == kParentTactile) {
            return "configuration";
        }
        return "l30_writes";
    }
    inline common::TransactionPlan request_plan(can::Message request, bool response = true) {
        common::TransactionPlan plan;
        const auto id = expected_response_id(request.id);
        plan.domain = response_key(id);
        const auto fields = parse_can_id(request.id);
        if (fields.access == kAccessWrite) {
            plan.command_family = write_command_family(fields.parent, fields.subcmd);
        }
        plan.frames = {request};
        plan.publish_after_send = true;
        if (response) {
            plan.keys = {plan.domain};
            plan.consume = [id](const can::Message &m) {
                if (!valid_frame(m, id)) {
                    return common::Progress::unmatched();
                }
                if (m.dlc < 3 || m.data[1] != kSingleFrameTransaction || static_cast<size_t>(m.data[0]) + 3 > m.dlc) {
                    return common::Progress::pending();
                }
                return common::Progress::complete(std::vector<uint8_t>(m.data.begin(), m.data.begin() + m.dlc));
            };
        }
        return plan;
    }
    inline common::TransactionPlan tactile_plan(uint8_t node, uint8_t host, uint8_t subcmd) {
        auto plan = request_plan(request_frame(node, host, kParentTactile, subcmd, kAccessRead, empty_request_payload(),
                                               kEmptyRequestBytes));
        const auto id = expected_response_id(plan.frames.front().id);
        plan.consume = [id, first = std::optional<std::vector<uint8_t>>{},
                        second = std::optional<std::vector<uint8_t>>{}](const can::Message &m) mutable {
            if (!valid_frame(m, id)) {
                return common::Progress::unmatched();
            }
            if (m.dlc < 3) {
                return common::Progress::pending();
            }
            const auto transaction = m.data[1];
            const std::vector<uint8_t> bytes(m.data.begin(), m.data.begin() + m.dlc);
            auto result = common::Progress::pending();
            try {
                if (m.data[2] != kStatusOk) {
                    if (transaction == kSingleFrameTransaction) {
                        if (m.data[0] != 0) {
                            return result;
                        }
                        check_response_status(bytes.data(), bytes.size());
                    }
                    if (transaction != kTactileFirstTransaction && transaction != kTactileSecondTransaction) {
                        return result;
                    }
                    static_cast<void>(assemble_tactile_payload(bytes, bytes));
                }
                if (transaction != kTactileFirstTransaction && transaction != kTactileSecondTransaction) {
                    throw ProtocolError("unexpected tactile transaction");
                }
                const auto expected =
                    transaction == kTactileFirstTransaction ? kTactileFirstLength : kTactileSecondLength;
                if (m.data[0] != expected) {
                    throw ProtocolError("tactile length mismatch: expected " + std::to_string(expected) +
                                        " bytes, got " + std::to_string(m.data[0]));
                }
                if (expected + 3 > m.dlc) {
                    throw ProtocolError("tactile frame payload too short");
                }
                auto &part = transaction == kTactileFirstTransaction ? first : second;
                const std::vector<uint8_t> useful(bytes.begin(),
                                                  bytes.begin() + static_cast<std::ptrdiff_t>(expected + 3));
                if (part && *part != useful) {
                    result.conflicting_duplicates = 1;
                    throw ProtocolError("conflicting duplicate tactile fragment");
                }
                const bool duplicate = part.has_value();
                part = useful;
                if (first && second) {
                    result = common::Progress::complete(assemble_tactile_payload(*first, *second));
                }
                result.duplicates = duplicate ? 1 : 0;
            } catch (const ProtocolError &e) {
                result.kind = common::ProgressKind::failed;
                result.error = std::current_exception();
                result.terminal_error = e.status() && transaction == kSingleFrameTransaction;
            }
            return result;
        };
        return plan;
    }
    struct Report {
        uint8_t subcmd;
        std::vector<uint8_t> payload;
    };
    inline common::Progress decode_report(const can::Message &m, uint8_t node, uint8_t host) {
        if (!m.is_extended || !m.is_fd || m.dlc > m.data.size() || m.dlc < 2 || m.data[1] != kSingleFrameTransaction ||
            static_cast<size_t>(m.data[0]) + 2 > m.dlc) {
            return common::Progress::unmatched();
        }
        FrameId fields;
        try {
            fields = parse_can_id(m.id);
        } catch (const std::invalid_argument &) {
            return common::Progress::unmatched();
        }
        if (fields.src_id != node || fields.dst_id != host || fields.access != kAccessRead ||
            fields.parent != kParentPeriodic) {
            return common::Progress::unmatched();
        }
        // Progress owns this payload through std::any; covered by ownership regression tests.
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        return common::Progress::complete(Report{fields.subcmd, {m.data.begin(), m.data.begin() + m.dlc}},
                                          common::Origin::report);
    }
} // namespace linkerbot::hand::l30::protocol
