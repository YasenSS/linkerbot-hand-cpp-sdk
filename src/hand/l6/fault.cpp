#include <linkerbot/hand/l6/fault_manager.hpp>
#include <linkerbot/hand/l6/protocol_backend.hpp>
namespace linkerbot::hand::l6 {
    FaultManager::FaultManager(uint32_t id, can::CANDispatcher &dispatcher)
        : FaultManager(id, detail::borrow_runtime(dispatcher, id)) {}
    FaultManager::FaultManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), channel_(std::move(runtime), "fault") {}
    FaultManager::~FaultManager() {
        try {
            stop();
        } catch (...) {
        }
        channel_.drain_calls();
    }
    void FaultManager::start() {
        channel_.start();
        const auto id = arbitration_id_;
        channel_.observe([id](const can::Message &msg) {
            return protocol::telemetry(id, kFaultOpcode, msg, protocol::common::Origin::unknown);
        });
    }
    void FaultManager::stop() { channel_.stop(); }
    FaultData FaultManager::get_blocking(std::chrono::milliseconds timeout) {
        const auto call = channel_.enter();
        const auto deadline = runtime::deadline_after(timeout);
        return channel_.get(protocol::read_telemetry(arbitration_id_, kFaultOpcode), deadline);
    }
    std::optional<FaultData> FaultManager::get_snapshot() const { return channel_.snapshot(); }
    void FaultManager::set_event_sink(std::function<void(const FaultData &)> sink) {
        channel_.set_sink(std::move(sink));
    }
    size_t FaultManager::pending_count() const { return channel_.pending_count(); }
    void FaultManager::cancel_waiters() { channel_.cancel(); }
    void FaultManager::resume_transactions() { channel_.start(); }
    void FaultManager::send_fault_request() {
        const auto call = channel_.enter();
        channel_.request(protocol::read_telemetry(arbitration_id_, kFaultOpcode),
                         runtime::deadline_after(std::chrono::milliseconds(100)));
    }
    void FaultManager::clear_faults() {
        const auto call = channel_.enter();
        channel_.command(protocol::command(arbitration_id_, kClearFaultOpcode, std::vector<uint8_t>(6, 1)));
    }
} // namespace linkerbot::hand::l6
