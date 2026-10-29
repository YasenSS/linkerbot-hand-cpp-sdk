#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20/current_manager.hpp>

#include <stdexcept>

namespace linkerbot::hand::l20 {
    CurrentManager::CurrentManager(uint32_t id, can::CANDispatcher &dispatcher)
        : CurrentManager(id, runtime::DeviceRuntime::borrow(dispatcher, "l20/" + std::to_string(id), {{id, false}})) {}
    CurrentManager::CurrentManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : id_(id), runtime_(std::move(runtime)), channel_(runtime_, "current") {}
    CurrentManager::~CurrentManager() = default;
    void CurrentManager::start() { channel_.start(); }
    void CurrentManager::stop() { channel_.stop(true); }
    CurrentData CurrentManager::get_blocking(std::chrono::milliseconds timeout) {
        if (timeout.count() <= 0) {
            throw ValidationError("timeout must be positive");
        }
        return channel_.get(protocol::read_current(id_), runtime::deadline_after(timeout));
    }
    std::optional<CurrentData> CurrentManager::get_snapshot() const { return channel_.snapshot(); }
    void CurrentManager::send_sense_request() {
        channel_.request(protocol::read_current(id_), runtime::deadline_after(std::chrono::milliseconds(200)));
    }
    void CurrentManager::set_event_sink(std::function<void(const CurrentData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

    CurrentLimitManager::CurrentLimitManager(uint32_t id, can::CANDispatcher &dispatcher)
        : CurrentLimitManager(id,
                              runtime::DeviceRuntime::borrow(dispatcher, "l20/" + std::to_string(id), {{id, false}})) {}
    CurrentLimitManager::CurrentLimitManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : id_(id), runtime_(std::move(runtime)), channel_(runtime_, "current_limit") {}
    CurrentLimitManager::~CurrentLimitManager() = default;
    void CurrentLimitManager::start() { channel_.start(); }
    void CurrentLimitManager::stop() { channel_.stop(true); }
    void CurrentLimitManager::set_current_limits(const std::array<uint8_t, kJointCount> &values) {
        channel_.request(protocol::write_current_limit(id_, values), runtime::Deadline::max());
    }
    std::optional<CurrentLimitData> CurrentLimitManager::get_snapshot() const { return channel_.snapshot(); }
    void CurrentLimitManager::set_event_sink(std::function<void(const CurrentLimitData &)> sink) {
        channel_.set_sink(std::move(sink));
    }
} // namespace linkerbot::hand::l20
