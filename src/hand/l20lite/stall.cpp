#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20lite/stall_manager.hpp>
namespace linkerbot::hand::l20lite {
    StallManager::StallManager(uint32_t id, can::CANDispatcher &dispatcher)
        : StallManager(id, runtime::DeviceRuntime::borrow(dispatcher, "l20lite/" + std::to_string(id), {{id, false}})) {
    }
    StallManager::StallManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), runtime_(std::move(runtime)), time_channel_(runtime_, "stall_time"),
          threshold_channel_(runtime_, "stall_threshold"), torque_channel_(runtime_, "stall_torque") {}
    StallManager::~StallManager() {
        try {
            stop();
            // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
            // NOLINTNEXTLINE(bugprone-empty-catch)
        } catch (...) {
        }
    }
    void StallManager::start() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        time_channel_.start();
        threshold_channel_.start();
        torque_channel_.start();
    }
    void StallManager::cancel_pending() {
        time_channel_.cancel();
        threshold_channel_.cancel();
        torque_channel_.cancel();
    }
    void StallManager::stop() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        cancel_pending();
        time_channel_.stop();
        threshold_channel_.stop();
        torque_channel_.stop();
    }
    void StallManager::set_times(const StallTime &value) { set_times(value, std::chrono::milliseconds{100}); }
    void StallManager::set_times(const StallTime &value, std::chrono::milliseconds timeout_ms) {
        const auto deadline = runtime::deadline_after(timeout_ms);
        const auto call = time_channel_.enter();
        const auto cancellation = time_channel_.token();
        value.validate();
        auto op = runtime_->operation("l20lite.stall_time", deadline, cancellation);
        auto plan = protocol::write_stall_bank(arbitration_id_, protocol::kStallTimeCmd, "stall_time",
                                               protocol::stall_times_to_raw(value));
        (void)time_channel_.get(std::move(plan), deadline);
    }
    void StallManager::set_times(const std::array<float, StallTime::kJointCount> &value) {
        set_times(StallTime::from_array(value));
    }
    void StallManager::set_times(const std::array<float, StallTime::kJointCount> &value,
                                 std::chrono::milliseconds timeout_ms) {
        set_times(StallTime::from_array(value), timeout_ms);
    }
    StallTimeData StallManager::get_times_blocking(std::chrono::milliseconds timeout_ms) {
        (void)runtime::deadline_after(timeout_ms);
        const auto call = time_channel_.enter();
        const auto cached = time_channel_.snapshot();
        if (!cached) {
            throw DeviceNotReadyError("stall time get requires a complete write response");
        }
        return *cached;
    }
    std::optional<StallTimeData> StallManager::get_time_snapshot() const { return time_channel_.snapshot(); }
    void StallManager::set_thresholds(const StallLimit &value) {
        set_thresholds(value, std::chrono::milliseconds{100});
    }
    void StallManager::set_thresholds(const StallLimit &value, std::chrono::milliseconds timeout_ms) {
        const auto deadline = runtime::deadline_after(timeout_ms);
        const auto call = threshold_channel_.enter();
        const auto cancellation = threshold_channel_.token();
        value.validate();
        auto op = runtime_->operation("l20lite.stall_threshold", deadline, cancellation);
        auto plan = protocol::write_stall_bank(arbitration_id_, protocol::kStallThresholdCmd, "stall_threshold",
                                               percents_to_raw(value.to_array()));
        (void)threshold_channel_.get(std::move(plan), deadline);
    }
    void StallManager::set_thresholds(const std::array<float, StallLimit::kJointCount> &value) {
        set_thresholds(StallLimit::from_array(value));
    }
    void StallManager::set_thresholds(const std::array<float, StallLimit::kJointCount> &value,
                                      std::chrono::milliseconds timeout_ms) {
        set_thresholds(StallLimit::from_array(value), timeout_ms);
    }
    StallLimitData StallManager::get_thresholds_blocking(std::chrono::milliseconds timeout_ms) {
        (void)runtime::deadline_after(timeout_ms);
        const auto call = threshold_channel_.enter();
        const auto cached = threshold_channel_.snapshot();
        if (!cached) {
            throw DeviceNotReadyError("stall threshold get requires a complete write response");
        }
        return *cached;
    }
    std::optional<StallLimitData> StallManager::get_threshold_snapshot() const { return threshold_channel_.snapshot(); }
    void StallManager::set_torques(const StallLimit &value) { set_torques(value, std::chrono::milliseconds{100}); }
    void StallManager::set_torques(const StallLimit &value, std::chrono::milliseconds timeout_ms) {
        const auto deadline = runtime::deadline_after(timeout_ms);
        const auto call = torque_channel_.enter();
        const auto cancellation = torque_channel_.token();
        value.validate();
        auto op = runtime_->operation("l20lite.stall_torque", deadline, cancellation);
        auto plan = protocol::write_stall_bank(arbitration_id_, protocol::kStallTorqueCmd, "stall_torque",
                                               percents_to_raw(value.to_array()));
        (void)torque_channel_.get(std::move(plan), deadline);
    }
    void StallManager::set_torques(const std::array<float, StallLimit::kJointCount> &value) {
        set_torques(StallLimit::from_array(value));
    }
    void StallManager::set_torques(const std::array<float, StallLimit::kJointCount> &value,
                                   std::chrono::milliseconds timeout_ms) {
        set_torques(StallLimit::from_array(value), timeout_ms);
    }
    StallLimitData StallManager::get_torques_blocking(std::chrono::milliseconds timeout_ms) {
        (void)runtime::deadline_after(timeout_ms);
        const auto call = torque_channel_.enter();
        const auto cached = torque_channel_.snapshot();
        if (!cached) {
            throw DeviceNotReadyError("stall torque get requires a complete write response");
        }
        return *cached;
    }
    std::optional<StallLimitData> StallManager::get_torque_snapshot() const { return torque_channel_.snapshot(); }
}
