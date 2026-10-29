#include "linkerbot/hand/o30/acceleration_manager.hpp"

#include "u8_io.hpp"

#include <utility>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    AccelerationManager::AccelerationManager(Client &client)
        : client_(&client), relay_(client.runtime(), "acceleration/relay_") {}

    void AccelerationManager::set_accelerations(const std::array<uint8_t, kJointCount> &accelerations,
                                                std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiAcceleration, accelerations, timeout_ms);
    }

    void AccelerationManager::set_all(uint8_t acceleration, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_all(*client_, protocol::kMiAcceleration, acceleration, timeout_ms);
    }

    AccelerationData AccelerationManager::get_blocking(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        AccelerationData data{};
        data.accelerations = detail::read_joints(*client_, protocol::kMiAcceleration, kind, timeout_ms);
        data.kind = kind;
        data.timestamp = client_->received_at();
        relay_.push(data);
        return data;
    }

    std::optional<AccelerationData> AccelerationManager::get_snapshot() const { return relay_.snapshot(); }

    void AccelerationManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void AccelerationManager::set_event_sink(std::function<void(const AccelerationData &)> sink) {
        relay_.set_sink(std::move(sink));
    }

} // namespace linkerbot::hand::o30
