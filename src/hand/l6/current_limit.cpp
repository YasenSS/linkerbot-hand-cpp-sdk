#include <linkerbot/hand/l6/current_limit_manager.hpp>

#include <linkerbot/hand/l6/frame.hpp>
#include <linkerbot/hand/l6/protocol_backend.hpp>

namespace linkerbot::hand::l6 {
    CurrentLimitManager::CurrentLimitManager(uint32_t id, can::CANDispatcher &dispatcher)
        : CurrentLimitManager(id, detail::borrow_runtime(dispatcher, id)) {}

    CurrentLimitManager::CurrentLimitManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), channel_(std::move(runtime), "current_limit") {}

    CurrentLimitManager::~CurrentLimitManager() {
        try {
            stop();
        } catch (...) {
        }
        channel_.drain_calls();
    }

    void CurrentLimitManager::start() {
        channel_.start();
        const auto id = arbitration_id_;
        channel_.observe([id](const can::Message &message) {
            return protocol::telemetry(id, kCurrentLimitOpcode, message, protocol::common::Origin::unknown);
        });
    }

    void CurrentLimitManager::stop() { channel_.stop(); }

    void CurrentLimitManager::set_current_limits(const CurrentLimit &limits) {
        const auto raw = limits.to_array();
        channel_.command(
            protocol::command(arbitration_id_, kCurrentLimitOpcode, std::vector<uint8_t>(raw.begin(), raw.end())));
    }

    void CurrentLimitManager::set_current_limits(const std::array<uint8_t, CurrentLimit::kJointCount> &values) {
        set_current_limits(CurrentLimit::from_array(values));
    }

    CurrentLimitData CurrentLimitManager::get_blocking(std::chrono::milliseconds timeout) {
        const auto call = channel_.enter();
        return channel_.get(protocol::read_telemetry(arbitration_id_, kCurrentLimitOpcode),
                            runtime::deadline_after(timeout));
    }

    std::optional<CurrentLimitData> CurrentLimitManager::get_snapshot() const { return channel_.snapshot(); }

    void CurrentLimitManager::set_event_sink(std::function<void(const CurrentLimitData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

    size_t CurrentLimitManager::pending_count() const { return channel_.pending_count(); }

    void CurrentLimitManager::cancel_waiters() { channel_.cancel(); }
    void CurrentLimitManager::resume_transactions() { channel_.start(); }

    void CurrentLimitManager::send_sense_request() {
        const auto call = channel_.enter();
        channel_.request(protocol::read_telemetry(arbitration_id_, kCurrentLimitOpcode),
                         runtime::deadline_after(std::chrono::milliseconds(100)));
    }
} // namespace linkerbot::hand::l6
