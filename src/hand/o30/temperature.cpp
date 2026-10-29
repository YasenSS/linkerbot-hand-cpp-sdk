#include "linkerbot/hand/o30/temperature_manager.hpp"

#include "u8_io.hpp"

#include <utility>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    TemperatureManager::TemperatureManager(Client &client)
        : client_(&client), relay_(client.runtime(), "temperature/relay_") {}

    void TemperatureManager::set_temperatures(const std::array<uint8_t, kJointCount> &temperatures,
                                              std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiTemperature, temperatures, timeout_ms);
    }

    TemperatureData TemperatureManager::get_blocking(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        TemperatureData data{};
        data.temperatures = detail::read_joints(*client_, protocol::kMiTemperature, kind, timeout_ms);
        data.kind = kind;
        data.timestamp = client_->received_at();
        relay_.push(data);
        return data;
    }

    std::optional<TemperatureData> TemperatureManager::get_snapshot() const { return relay_.snapshot(); }

    void TemperatureManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void TemperatureManager::set_event_sink(std::function<void(const TemperatureData &)> sink) {
        relay_.set_sink(std::move(sink));
    }

} // namespace linkerbot::hand::o30
