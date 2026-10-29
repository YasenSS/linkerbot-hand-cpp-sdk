#include "linkerbot/hand/o30/torque_manager.hpp"

#include "u8_io.hpp"

#include <utility>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    TorqueManager::TorqueManager(Client &client) : client_(&client), relay_(client.runtime(), "torque") {}

    TorqueData TorqueManager::get_torques(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        TorqueData data{};
        data.torques = detail::read_joints(*client_, protocol::kMiTorque, ValueKind::kActual, timeout_ms);
        data.timestamp = client_->received_at();
        relay_.push(data);
        return data;
    }

    std::optional<TorqueData> TorqueManager::get_snapshot() const { return relay_.snapshot(); }

    void TorqueManager::send_sense_request() { static_cast<void>(get_torques()); }

    void TorqueManager::set_event_sink(std::function<void(const TorqueData &)> sink) {
        relay_.set_sink(std::move(sink));
    }

} // namespace linkerbot::hand::o30
