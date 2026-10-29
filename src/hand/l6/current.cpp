#include <linkerbot/hand/l6/current_manager.hpp>
#include <linkerbot/hand/l6/protocol_backend.hpp>
namespace linkerbot::hand::l6 {
    CurrentManager::CurrentManager(uint32_t id, can::CANDispatcher &dispatcher)
        : CurrentManager(id, detail::borrow_runtime(dispatcher, id)) {}
    CurrentManager::CurrentManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), channel_(std::move(runtime), "current") {}
    CurrentManager::~CurrentManager() {
        try {
            stop();
        } catch (...) {
        }
        channel_.drain_calls();
    }
    void CurrentManager::start() {
        channel_.start();
        const auto id = arbitration_id_;
        channel_.observe([id](const can::Message &msg) {
            return protocol::telemetry(id, kCurrentOpcode, msg, protocol::common::Origin::unknown);
        });
    }
    void CurrentManager::stop() { channel_.stop(); }
    CurrentData CurrentManager::get_blocking(std::chrono::milliseconds timeout) {
        const auto call = channel_.enter();
        const auto deadline = runtime::deadline_after(timeout);
        return channel_.get(protocol::read_telemetry(arbitration_id_, kCurrentOpcode), deadline);
    }
    std::optional<CurrentData> CurrentManager::get_snapshot() const { return channel_.snapshot(); }
    void CurrentManager::set_event_sink(std::function<void(const CurrentData &)> sink) {
        channel_.set_sink(std::move(sink));
    }
    size_t CurrentManager::pending_count() const { return channel_.pending_count(); }
    void CurrentManager::cancel_waiters() { channel_.cancel(); }
    void CurrentManager::resume_transactions() { channel_.start(); }
    void CurrentManager::send_sense_request() {
        const auto call = channel_.enter();
        channel_.request(protocol::read_telemetry(arbitration_id_, kCurrentOpcode),
                         runtime::deadline_after(std::chrono::milliseconds(100)));
    }
} // namespace linkerbot::hand::l6
