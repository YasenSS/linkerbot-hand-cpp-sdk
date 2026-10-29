#include "linkerbot/hand/o30/torque_manager.hpp"

#include "u8_io.hpp"

#include <utility>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    TorqueManager::TorqueManager(Client &client)
        : client_(&client), relay_(client.runtime(), "torque"), limit_relay_(client.runtime(), "torque_limit") {}

    void TorqueManager::set_torque_limits(const std::array<uint8_t, kJointCount> &torques,
                                          std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiTorque, torques, timeout_ms);
    }

    void TorqueManager::set_all_torque_limits(uint8_t torque, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_all(*client_, protocol::kMiTorque, torque, timeout_ms);
    }

    TorqueData TorqueManager::get_torques(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        TorqueData data{};
        data.torques = detail::read_joints(*client_, protocol::kMiTorque, ValueKind::kActual, timeout_ms);
        data.timestamp = client_->received_at();
        relay_.push(data);
        return data;
    }

    TorqueLimitData TorqueManager::get_torque_limits(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        TorqueLimitData data{detail::read_joints(*client_, protocol::kMiTorque, ValueKind::kSetpoint, timeout_ms),
                             client_->received_at()};
        limit_relay_.push(data);
        return data;
    }

    std::optional<TorqueData> TorqueManager::get_snapshot() const { return relay_.snapshot(); }
    std::optional<TorqueLimitData> TorqueManager::get_torque_limit_snapshot() const { return limit_relay_.snapshot(); }

    void TorqueManager::send_sense_request() { static_cast<void>(get_torques()); }

    void TorqueManager::set_event_sink(std::function<void(const TorqueData &)> sink) {
        relay_.set_sink(std::move(sink));
    }

    void TorqueManager::set_limit_event_sink(std::function<void(const TorqueLimitData &)> sink) {
        limit_relay_.set_sink(std::move(sink));
    }

} // namespace linkerbot::hand::o30
