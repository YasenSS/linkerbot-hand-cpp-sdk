#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20lite/tip_force_manager.hpp>
namespace linkerbot::hand::l20lite {
    namespace {
        constexpr const char *names[]{"thumb", "index", "middle", "ring", "pinky"};
        constexpr uint8_t commands[]{0x28, 0x29, 0x30, 0x31, 0x32};
        std::string domain(size_t i) { return std::string("tip_force_finger_") + names[i]; }
    }
    TipForceManager::TipForceManager(uint32_t id, can::CANDispatcher &dispatcher)
        : TipForceManager(id,
                          runtime::DeviceRuntime::borrow(dispatcher, "l20lite/" + std::to_string(id), {{id, false}})) {}
    TipForceManager::TipForceManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), runtime_(std::move(runtime)), channel_(runtime_, "tip_force") {
        for (size_t i = 0; i < 5; ++i) {
            fingers_[i] = std::make_unique<detail::Channel<TipFinger>>(runtime_, domain(i));
        }
    }
    TipForceManager::~TipForceManager() {
        try {
            cancel_pending();
            // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
            // NOLINTNEXTLINE(bugprone-empty-catch)
        } catch (...) {
        }
    }
    void TipForceManager::start() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        channel_.start();
        for (auto &finger : fingers_) {
            finger->start();
        }
    }
    void TipForceManager::cancel_pending() {
        channel_.cancel();
        for (auto &finger : fingers_) {
            finger->cancel();
        }
    }
    void TipForceManager::stop() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        cancel_pending();
        channel_.stop();
        for (auto &finger : fingers_) {
            finger->stop();
        }
    }
    TipForceData TipForceManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        const auto deadline = runtime::deadline_after(timeout_ms);
        return channel_.get(protocol::read_tip_force(arbitration_id_), deadline);
    }
    TipFinger TipForceManager::get_finger_blocking(const std::string &name, std::chrono::milliseconds timeout_ms) {
        const auto deadline = runtime::deadline_after(timeout_ms);
        for (size_t i = 0; i < 5; ++i) {
            if (name == names[i]) {
                return fingers_[i]->get(protocol::read_tip_finger(arbitration_id_, commands[i], domain(i)), deadline);
            }
        }
        throw ValidationError("Unknown finger: " + name);
    }
    std::optional<TipForceData> TipForceManager::get_snapshot() const { return channel_.snapshot(); }
    void TipForceManager::set_event_sink(std::function<void(const TipForceData &)> sink) {
        channel_.set_sink(std::move(sink));
    }
    void TipForceManager::send_sense_request() {
        channel_.request(protocol::read_tip_force(arbitration_id_),
                         runtime::deadline_after(std::chrono::milliseconds(100)));
    }
}
