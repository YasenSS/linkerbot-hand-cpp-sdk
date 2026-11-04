#include "linkerbot/hand/o30/speed_manager.hpp"

#include "u8_io.hpp"

#include <utility>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    SpeedManager::SpeedManager(Client &client) : client_(&client), relay_(client.runtime(), "speed/relay_") {}

    void SpeedManager::set_speeds(const std::array<uint8_t, kJointCount> &speeds,
                                  std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiSpeed, speeds, timeout_ms);
    }

    void SpeedManager::set_all(uint8_t speed, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_all(*client_, {protocol::kMiSpeed, speed}, timeout_ms);
    }

    SpeedData SpeedManager::get_blocking(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        SpeedData data{};
        data.speeds = detail::read_joints(*client_, protocol::kMiSpeed, kind, timeout_ms);
        data.kind = kind;
        data.timestamp = client_->received_at();
        relay_.push(data);
        return data;
    }

    std::optional<SpeedData> SpeedManager::get_snapshot() const { return relay_.snapshot(); }

    void SpeedManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void SpeedManager::set_event_sink(std::function<void(const SpeedData &)> sink) { relay_.set_sink(std::move(sink)); }

} // namespace linkerbot::hand::o30
