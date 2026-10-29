#include "linkerbot/hand/o30/stall_manager.hpp"

#include "u8_io.hpp"

#include <utility>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    StallManager::StallManager(Client &client)
        : client_(&client), times_relay_(client.runtime(), "stall/times_relay_"),
          thresholds_relay_(client.runtime(), "stall/thresholds_relay_"),
          hold_relay_(client.runtime(), "stall/hold_relay_") {}

    void StallManager::set_times(const std::array<uint8_t, kJointCount> &times, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiStallTime, times, timeout_ms);
    }

    void StallManager::set_thresholds(const std::array<uint8_t, kJointCount> &thresholds,
                                      std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiStallThreshold, thresholds, timeout_ms);
    }

    void StallManager::set_hold_currents(const std::array<uint8_t, kJointCount> &currents,
                                         std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiStallHoldCurrent, currents, timeout_ms);
    }

    StallTimeData StallManager::get_times(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        StallTimeData data{};
        data.times = detail::read_joints(*client_, protocol::kMiStallTime, kind, timeout_ms);
        data.kind = kind;
        data.timestamp = client_->received_at();
        times_relay_.push(data);
        return data;
    }

    StallThresholdData StallManager::get_thresholds(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        StallThresholdData data{};
        data.thresholds = detail::read_joints(*client_, protocol::kMiStallThreshold, kind, timeout_ms);
        data.kind = kind;
        data.timestamp = client_->received_at();
        thresholds_relay_.push(data);
        return data;
    }

    StallHoldCurrentData StallManager::get_hold_currents(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        StallHoldCurrentData data{};
        data.currents = detail::read_joints(*client_, protocol::kMiStallHoldCurrent, kind, timeout_ms);
        data.kind = kind;
        data.timestamp = client_->received_at();
        hold_relay_.push(data);
        return data;
    }

    std::optional<StallTimeData> StallManager::get_times_snapshot() const { return times_relay_.snapshot(); }

    std::optional<StallThresholdData> StallManager::get_thresholds_snapshot() const {
        return thresholds_relay_.snapshot();
    }

    std::optional<StallHoldCurrentData> StallManager::get_hold_currents_snapshot() const {
        return hold_relay_.snapshot();
    }

    void StallManager::send_sense_request() { static_cast<void>(get_times()); }

    void StallManager::set_event_sink(std::function<void(const StallTimeData &)> sink) {
        times_relay_.set_sink(std::move(sink));
    }

} // namespace linkerbot::hand::o30
