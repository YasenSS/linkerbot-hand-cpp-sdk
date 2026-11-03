#include <linkerbot/hand/l6/protocol_backend.hpp>
#include <linkerbot/hand/l6/torque_manager.hpp>
namespace linkerbot::hand::l6 {
    TorqueManager::TorqueManager(uint32_t id, can::CANDispatcher &dispatcher)
        : TorqueManager(id, detail::borrow_runtime(dispatcher, id)) {}
    TorqueManager::TorqueManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), channel_(std::move(runtime), "torque") {}
    TorqueManager::~TorqueManager() {
        try {
            stop();
            // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
            // NOLINTNEXTLINE(bugprone-empty-catch)
        } catch (...) {
        }
        channel_.drain_calls();
    }
    void TorqueManager::start() {
        channel_.start();
        const auto id = arbitration_id_;
        channel_.observe([id](const can::Message &msg) {
            return protocol::telemetry(id, kTorqueOpcode, msg, protocol::common::Origin::unknown);
        });
    }
    void TorqueManager::stop() { channel_.stop(); }
    TorqueData TorqueManager::get_blocking(std::chrono::milliseconds timeout) {
        const auto call = channel_.enter();
        const auto deadline = runtime::deadline_after(timeout);
        return channel_.get(protocol::read_telemetry(arbitration_id_, kTorqueOpcode), deadline);
    }
    std::optional<TorqueData> TorqueManager::get_snapshot() const { return channel_.snapshot(); }
    void TorqueManager::set_event_sink(std::function<void(const TorqueData &)> sink) {
        channel_.set_sink(std::move(sink));
    }
    size_t TorqueManager::pending_count() const { return channel_.pending_count(); }
    void TorqueManager::cancel_waiters() { channel_.cancel(); }
    void TorqueManager::resume_transactions() { channel_.start(); }
    void TorqueManager::send_sense_request() {
        const auto call = channel_.enter();
        channel_.request(protocol::read_telemetry(arbitration_id_, kTorqueOpcode),
                         runtime::deadline_after(std::chrono::milliseconds(100)));
    }
    void TorqueManager::set_torques(const Torque &values) {
        const auto call = channel_.enter();
        values.validate();
        const auto values_raw = values.to_array();
        std::vector<uint8_t> raw(values_raw.begin(), values_raw.end());
        channel_.command(protocol::command(arbitration_id_, kTorqueOpcode, raw));
    }
    void TorqueManager::set_torques(const std::array<uint8_t, Torque::kJointCount> &values) {
        const auto call = channel_.enter();
        set_torques(Torque::from_array(values));
    }
} // namespace linkerbot::hand::l6
