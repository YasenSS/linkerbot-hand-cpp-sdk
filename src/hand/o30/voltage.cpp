#include "linkerbot/hand/o30/voltage_manager.hpp"

#include "u8_io.hpp"

#include <utility>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    VoltageManager::VoltageManager(Client &client) : client_(&client), relay_(client.runtime(), "voltage/relay_") {}

    void VoltageManager::set_voltages(const std::array<uint8_t, kJointCount> &voltages,
                                      std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiVoltage, voltages, timeout_ms);
    }

    void VoltageManager::set_all(uint8_t voltage, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_all(*client_, protocol::kMiVoltage, voltage, timeout_ms);
    }

    VoltageData VoltageManager::get_blocking(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        VoltageData data{};
        data.voltages = detail::read_joints(*client_, protocol::kMiVoltage, kind, timeout_ms);
        data.kind = kind;
        data.timestamp = client_->received_at();
        relay_.push(data);
        return data;
    }

    std::optional<VoltageData> VoltageManager::get_snapshot() const { return relay_.snapshot(); }

    void VoltageManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void VoltageManager::set_event_sink(std::function<void(const VoltageData &)> sink) {
        relay_.set_sink(std::move(sink));
    }

} // namespace linkerbot::hand::o30
