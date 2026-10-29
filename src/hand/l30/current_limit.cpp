#include "linkerbot/hand/l30/current_limit_manager.hpp"

#include <chrono>
#include <utility>

namespace linkerbot::hand::l30 {

    CurrentLimitManager::CurrentLimitManager(Client &client) : client_(&client), relay_(client, "l30.current_limit") {}

    void CurrentLimitManager::set_current_limits(const std::array<uint16_t, kJointCount> &current_limits) {
        const auto call = client_->enter();
        validate_u16_range(current_limits, kCurrentLimitMin, kCurrentLimitMax, "current_limits");
        client_->send_no_ack(protocol::kParentControl, protocol::kSubCurrentLimit,
                             protocol::encode_u16_vector(current_limits), protocol::kVectorWireBytes);
        relay_.push(CurrentLimitData{current_limits, std::chrono::steady_clock::now()});
    }

    void CurrentLimitManager::set_all(uint16_t current_limit) {
        const auto call = client_->enter();
        std::array<uint16_t, kJointCount> current_limits{};
        current_limits.fill(current_limit);
        set_current_limits(current_limits);
    }

    std::optional<CurrentLimitData> CurrentLimitManager::get_snapshot() const { return relay_.snapshot(); }

    void CurrentLimitManager::set_event_sink(std::function<void(const CurrentLimitData &)> sink) {
        relay_.set_sink(std::move(sink));
    }

} // namespace linkerbot::hand::l30
