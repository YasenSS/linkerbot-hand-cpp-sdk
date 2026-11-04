#include "linkerbot/hand/o30/current_manager.hpp"

#include "u8_io.hpp"

#include <utility>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    CurrentManager::CurrentManager(Client &client)
        : client_(&client), relay_(client.runtime(), "current"), limit_relay_(client.runtime(), "current_limit") {}

    void CurrentManager::set_current_limits(const std::array<uint8_t, kJointCount> &currents,
                                            std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiCurrent, currents, timeout_ms);
    }

    void CurrentManager::set_all_current_limits(uint8_t current, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_all(*client_, {protocol::kMiCurrent, current}, timeout_ms);
    }

    CurrentData CurrentManager::get_currents(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        CurrentData data{};
        data.currents = detail::read_joints(*client_, protocol::kMiCurrent, ValueKind::kActual, timeout_ms);
        data.timestamp = client_->received_at();
        relay_.push(data);
        return data;
    }

    CurrentLimitData CurrentManager::get_current_limits(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        CurrentLimitData data{detail::read_joints(*client_, protocol::kMiCurrent, ValueKind::kSetpoint, timeout_ms),
                              client_->received_at()};
        limit_relay_.push(data);
        return data;
    }

    std::optional<CurrentData> CurrentManager::get_snapshot() const { return relay_.snapshot(); }
    std::optional<CurrentLimitData> CurrentManager::get_current_limit_snapshot() const {
        return limit_relay_.snapshot();
    }

    void CurrentManager::send_sense_request() { static_cast<void>(get_currents()); }

    void CurrentManager::set_event_sink(std::function<void(const CurrentData &)> sink) {
        relay_.set_sink(std::move(sink));
    }

    void CurrentManager::set_limit_event_sink(std::function<void(const CurrentLimitData &)> sink) {
        limit_relay_.set_sink(std::move(sink));
    }

} // namespace linkerbot::hand::o30
