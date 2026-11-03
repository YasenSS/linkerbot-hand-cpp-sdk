#include <linkerbot/hand/l6/protocol_backend.hpp>
#include <linkerbot/hand/l6/temperature_manager.hpp>
namespace linkerbot::hand::l6 {
    TemperatureManager::TemperatureManager(uint32_t id, can::CANDispatcher &dispatcher)
        : TemperatureManager(id, detail::borrow_runtime(dispatcher, id)) {}
    TemperatureManager::TemperatureManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), channel_(std::move(runtime), "temperature") {}
    TemperatureManager::~TemperatureManager() {
        try {
            stop();
            // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
            // NOLINTNEXTLINE(bugprone-empty-catch)
        } catch (...) {
        }
        channel_.drain_calls();
    }
    void TemperatureManager::start() {
        channel_.start();
        const auto id = arbitration_id_;
        channel_.observe([id](const can::Message &msg) {
            return protocol::telemetry(id, kTemperatureOpcode, msg, protocol::common::Origin::unknown);
        });
    }
    void TemperatureManager::stop() { channel_.stop(); }
    TemperatureData TemperatureManager::get_blocking(std::chrono::milliseconds timeout) {
        const auto call = channel_.enter();
        const auto deadline = runtime::deadline_after(timeout);
        return channel_.get(protocol::read_telemetry(arbitration_id_, kTemperatureOpcode), deadline);
    }
    std::optional<TemperatureData> TemperatureManager::get_snapshot() const { return channel_.snapshot(); }
    void TemperatureManager::set_event_sink(std::function<void(const TemperatureData &)> sink) {
        channel_.set_sink(std::move(sink));
    }
    size_t TemperatureManager::pending_count() const { return channel_.pending_count(); }
    void TemperatureManager::cancel_waiters() { channel_.cancel(); }
    void TemperatureManager::resume_transactions() { channel_.start(); }
    void TemperatureManager::send_sense_request() {
        const auto call = channel_.enter();
        channel_.request(protocol::read_telemetry(arbitration_id_, kTemperatureOpcode),
                         runtime::deadline_after(std::chrono::milliseconds(100)));
    }
} // namespace linkerbot::hand::l6
