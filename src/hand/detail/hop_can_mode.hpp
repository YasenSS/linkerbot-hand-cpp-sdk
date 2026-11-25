#pragma once

// Runtime helper for HOP MI 0x35 CAN mode reads and the single mode write.
// Protocol headers stay value-only; this file is the only place that waits.

#include <any>
#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <linkerbot/hand/api/can_configuration.hpp>
#include <linkerbot/hand/protocol/hop.hpp>
#include <linkerbot/hand/protocol/transaction.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>

namespace linkerbot::hand::detail {
    inline constexpr uint8_t kHopCanMi = 0x35;
    inline constexpr uint8_t kHopCanRequestIdSi = 0x00;
    inline constexpr uint8_t kHopCanInterfaceSi = 0x06;
    inline constexpr uint8_t kHopCanSaveSi = 0x1E;

    struct HopCanFrameResult {
        std::vector<uint8_t> payload;
        bool is_fd = false;
    };

    [[nodiscard]] inline std::chrono::milliseconds hop_can_operation_budget(std::chrono::milliseconds timeout) {
        constexpr int kSteps = 8;
        if (timeout.count() <= 0) {
            return timeout;
        }
        if (timeout.count() > std::chrono::milliseconds::max().count() / kSteps) {
            return std::chrono::milliseconds::max();
        }
        return timeout * kSteps;
    }

    [[nodiscard]] inline std::optional<HopCanFrameResult>
    exchange_hop_frame(runtime::DeviceRuntime &runtime, uint32_t request_id, bool send_is_fd, uint8_t mi, uint8_t si,
                       uint8_t edl, const std::vector<uint8_t> &payload, bool rts, bool accept_either_format,
                       bool configuration_write, runtime::Deadline deadline) {
        if (request_id > 0x3FEU) {
            throw ValidationError("HOP request ID must be 0..0x3FE");
        }
        const auto response_id = request_id | 0x400U;
        auto frame = protocol::hop::build_message(request_id, mi, si, edl, payload, rts);
        if (!send_is_fd) {
            const auto useful = protocol::hop::kHeaderBytes + payload.size();
            if (useful > 8) {
                throw ValidationError("HOP CAN 2.0 frame exceeds 8 bytes");
            }
            frame.is_fd = false;
            frame.is_brs = false;
            frame.dlc = static_cast<uint8_t>(useful);
        }
        auto answered_fd = std::make_shared<std::optional<bool>>();
        protocol::TransactionPlan plan;
        plan.keys = {"hop-can-configuration"};
        plan.domain = "hop/can-configuration";
        plan.poison_on_timeout = false;
        plan.late_guard = std::chrono::milliseconds{0};
        plan.preserve_last_success = true;
        if (configuration_write) {
            plan.command_family = "configuration";
            plan.publish_after_send = true;
        }
        plan.frames = {std::move(frame)};
        plan.consume = [response_id, mi, si, edl, rts, send_is_fd, accept_either_format,
                        answered_fd](const can::Message &message) {
            using protocol::Progress;
            if (message.id != response_id || message.is_extended) {
                return Progress::unmatched();
            }
            if (!accept_either_format && message.is_fd != send_is_fd) {
                return Progress::unmatched();
            }
            if (message.dlc < protocol::hop::kHeaderBytes) {
                return Progress::unmatched();
            }
            if (message.data[0] != protocol::hop::ctrl_byte(mi, rts)) {
                return Progress::unmatched();
            }
            if (message.data[1] != si) {
                return Progress::unmatched();
            }
            if (message.is_fd) {
                if (message.dlc > 64 || protocol::hop::canfd_wire_bytes(message.dlc) != message.dlc) {
                    throw ProtocolError("invalid HOP CAN FD wire length");
                }
            } else if (message.dlc > 8) {
                throw ProtocolError("invalid HOP CAN 2.0 wire length");
            }
            const auto parsed = protocol::hop::parse_frame(message.data.data(), message.dlc);
            if (parsed.edl != edl) {
                throw ProtocolError("HOP response EDL does not match requested range");
            }
            *answered_fd = message.is_fd;
            return Progress::complete(parsed.payload);
        };
        try {
            auto sample = runtime.execute(std::move(plan), deadline);
            if (!answered_fd->has_value()) {
                throw ProtocolError("HOP CAN configuration reply did not identify its wire format");
            }
            HopCanFrameResult result;
            result.payload = std::any_cast<std::vector<uint8_t>>(std::move(sample.value));
            result.is_fd = **answered_fd;
            return result;
        } catch (const TimeoutError &) {
            return std::nullopt;
        }
    }

    template <typename Exchange, typename DeadlineFactory>
    [[nodiscard]] HopCanConfiguration read_hop_can_configuration(bool wire_is_fd, Exchange &&exchange,
                                                                 DeadlineFactory &&deadline) {
        auto request = exchange(wire_is_fd, kHopCanMi, kHopCanRequestIdSi, 2, std::vector<uint8_t>{}, true, false,
                                false, deadline());
        if (!request) {
            throw TimeoutError("HOP CAN request ID read timed out");
        }
        auto interface = exchange(wire_is_fd, kHopCanMi, kHopCanInterfaceSi, 4, std::vector<uint8_t>{}, true, false,
                                  false, deadline());
        if (!interface) {
            throw TimeoutError("HOP CAN interface read timed out");
        }
        if (request->is_fd != wire_is_fd || interface->is_fd != wire_is_fd) {
            throw ProtocolError("HOP CAN configuration reply format does not match the active wire");
        }
        return parse_hop_can_configuration(request->payload.data(), request->payload.size(), interface->payload.data(),
                                           interface->payload.size());
    }

    template <typename Exchange, typename DeadlineFactory>
    [[nodiscard]] std::optional<HopCanFrameResult>
    probe_hop_can_interface(Exchange &&exchange, DeadlineFactory &&deadline, std::initializer_list<bool> formats) {
        bool saw_fd = false;
        bool saw_classic = false;
        for (const bool fd : formats) {
            if (fd ? saw_fd : saw_classic) {
                continue;
            }
            if (fd) {
                saw_fd = true;
            } else {
                saw_classic = true;
            }
            if (auto reply = exchange(fd, kHopCanMi, kHopCanInterfaceSi, 4, std::vector<uint8_t>{}, true, true, false,
                                      deadline())) {
                return reply;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline HopCanConfiguration configuration_from_interface(const HopCanConfiguration &before,
                                                                          const std::vector<uint8_t> &interface) {
        const std::array<uint8_t, 2> request_id{static_cast<uint8_t>(before.request_id & 0xFFU),
                                                static_cast<uint8_t>((before.request_id >> 8U) & 0xFFU)};
        return parse_hop_can_configuration(request_id.data(), request_id.size(), interface.data(), interface.size());
    }

    inline void require_confirmed_mode(const HopCanConfiguration &before, const HopCanConfiguration &after,
                                       HopCanInterfaceMode mode) {
        if (after.request_id != before.request_id || after.interface_mode != mode || after.brs != before.brs ||
            after.nominal_bitrate != before.nominal_bitrate || after.data_bitrate != before.data_bitrate) {
            throw ProtocolError("Target configuration/BRS/bitrates not confirmed; do not repeat write");
        }
    }

    template <typename Exchange, typename DeadlineFactory>
    [[nodiscard]] HopCanModeSwitchResult set_hop_can_interface_mode(bool current_fd, HopCanInterfaceMode mode,
                                                                    bool save_to_flash, Exchange &&exchange,
                                                                    DeadlineFactory &&deadline) {
        if (!is_hop_can_interface_mode(mode)) {
            throw ValidationError("CAN interface mode must be classic CAN 2.0 (1) or CAN FD (2)");
        }
        HopCanModeSwitchResult result;
        result.before = read_hop_can_configuration(current_fd, exchange, deadline);
        result.after = result.before;
        result.active_wire_is_fd = current_fd;
        if (mode == HopCanInterfaceMode::kCan20 && result.before.brs) {
            throw ValidationError("Disable BRS separately before selecting classic CAN");
        }
        const bool target_fd = mode == HopCanInterfaceMode::kCanFd;
        if (result.before.interface_mode != mode) {
            result.write_sent = true;
            const std::vector<uint8_t> payload{static_cast<uint8_t>(mode)};
            // One write. A missing or wrong-format reply is not a reason to send it again.
            auto ack = exchange(current_fd, kHopCanMi, kHopCanInterfaceSi, 1, payload, false, true, true, deadline());
            std::optional<HopCanFrameResult> verified;
            if (ack) {
                verified = probe_hop_can_interface(exchange, deadline, {ack->is_fd, !ack->is_fd});
            } else {
                verified = probe_hop_can_interface(exchange, deadline, {target_fd, current_fd});
            }
            if (!verified) {
                throw TimeoutError("Device mode could not be verified; no automatic write retry");
            }
            result.active_wire_is_fd = verified->is_fd;
            result.after = configuration_from_interface(result.before, verified->payload);
            require_confirmed_mode(result.before, result.after, mode);
        }
        if (save_to_flash) {
            auto saved = exchange(result.active_wire_is_fd, kHopCanMi, kHopCanSaveSi, 1, std::vector<uint8_t>{0x01},
                                  false, true, true, deadline());
            if (!saved || saved->payload.size() != 1) {
                throw ProtocolError("HOP flash save completion was not confirmed; save was not retried");
            }
            result.saved_to_flash = true;
            result.active_wire_is_fd = saved->is_fd;
            auto refreshed =
                probe_hop_can_interface(exchange, deadline, {result.active_wire_is_fd, !result.active_wire_is_fd});
            if (!refreshed) {
                throw TimeoutError("Flash save was sent once but the CAN configuration was not reread");
            }
            result.active_wire_is_fd = refreshed->is_fd;
            result.after = configuration_from_interface(result.before, refreshed->payload);
            require_confirmed_mode(result.before, result.after, mode);
        }
        result.active_format_changed = result.active_wire_is_fd != current_fd;
        return result;
    }

    template <typename Runtime> [[nodiscard]] auto hop_can_exchange(Runtime &runtime, uint32_t request_id) {
        return [&runtime, request_id](bool send_is_fd, uint8_t mi, uint8_t si, uint8_t edl,
                                      const std::vector<uint8_t> &payload, bool rts, bool accept_either_format,
                                      bool configuration_write, runtime::Deadline deadline) {
            return exchange_hop_frame(runtime, request_id, send_is_fd, mi, si, edl, payload, rts, accept_either_format,
                                      configuration_write, deadline);
        };
    }

    [[nodiscard]] inline HopCanConfiguration read_hop_can_configuration_blocking(runtime::DeviceRuntime &runtime,
                                                                                 uint32_t request_id, bool wire_is_fd,
                                                                                 std::chrono::milliseconds timeout) {
        if (timeout.count() <= 0) {
            throw ValidationError("CAN configuration timeout must be positive");
        }
        auto operation = runtime.operation(runtime::deadline_after(hop_can_operation_budget(timeout)));
        return read_hop_can_configuration(wire_is_fd, hop_can_exchange(runtime, request_id),
                                          [timeout] { return runtime::deadline_after(timeout); });
    }

    [[nodiscard]] inline HopCanModeSwitchResult
    set_hop_can_interface_mode_blocking(runtime::DeviceRuntime &runtime, uint32_t request_id, bool wire_is_fd,
                                        HopCanInterfaceMode mode, bool save_to_flash,
                                        std::chrono::milliseconds timeout) {
        if (timeout.count() <= 0) {
            throw ValidationError("CAN configuration timeout must be positive");
        }
        if (!is_hop_can_interface_mode(mode)) {
            throw ValidationError("CAN interface mode must be classic CAN 2.0 (1) or CAN FD (2)");
        }
        auto operation = runtime.operation(runtime::deadline_after(hop_can_operation_budget(timeout)));
        return set_hop_can_interface_mode(wire_is_fd, mode, save_to_flash, hop_can_exchange(runtime, request_id),
                                          [timeout] { return runtime::deadline_after(timeout); });
    }
} // namespace linkerbot::hand::detail
