#include "linkerbot/hand/o6/stall.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/o6/stall_manager.hpp>
#include <stdexcept>
#include <utility>

namespace linkerbot::hand::o6 {

    namespace {
        uint8_t ms_to_raw(float ms) {
            if (ms <= 0.0f) {
                return 0;
            }
            const auto rounded = std::lround(static_cast<double>(ms) / 10.0);
            if (rounded > 255) {
                return 255;
            }
            return static_cast<uint8_t>(rounded);
        }

        uint8_t limit_to_raw(float value) {
            const auto rounded = std::lround(static_cast<double>(value) * 255.0 / 1000.0);
            if (rounded < 0) {
                return 0;
            }
            if (rounded > 255) {
                return 255;
            }
            return static_cast<uint8_t>(rounded);
        }

        std::array<uint8_t, StallTime::kJointCount> times_to_raw(const StallTime &times) {
            auto arr = times.to_array();
            std::array<uint8_t, StallTime::kJointCount> raw{};
            for (size_t i = 0; i < StallTime::kJointCount; ++i) {
                raw[i] = ms_to_raw(arr[i]);
            }
            return raw;
        }

        std::array<uint8_t, StallLimit::kJointCount> limits_to_raw(const StallLimit &limits) {
            auto arr = limits.to_array();
            std::array<uint8_t, StallLimit::kJointCount> raw{};
            for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
                raw[i] = limit_to_raw(arr[i]);
            }
            return raw;
        }

        void check_timeout(std::chrono::milliseconds timeout_ms) {
            if (timeout_ms.count() <= 0) {
                throw ValidationError("timeout_ms must be positive");
            }
        }
    } // namespace

    StallManager::StallManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher)
        : StallManager(arbitration_id,
                       runtime::DeviceRuntime::borrow(dispatcher, "o6/" + std::to_string(arbitration_id),
                                                      {{arbitration_id, false}})) {}

    StallManager::StallManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          time_channel_(runtime_, "stall_time"), threshold_channel_(runtime_, "stall_threshold"),
          torque_channel_(runtime_, "stall_torque") {}

    StallManager::~StallManager() {
        try {
            cancel_pending();
        } catch (...) {
        }
    }

    void StallManager::start() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
        const auto id = arbitration_id_;
        time_channel_.start([id](const can::Message &message) { return protocol::decode_stall_time(id, message); });
        threshold_channel_.start(
            [id](const can::Message &message) { return protocol::decode_stall_threshold(id, message); });
        torque_channel_.start([id](const can::Message &message) { return protocol::decode_stall_torque(id, message); });
    }

    void StallManager::stop() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
        cancel_pending();
        time_channel_.stop();
        threshold_channel_.stop();
        torque_channel_.stop();
    }

    void StallManager::set_times(const StallTime &times) {
        times.validate();
        time_channel_.command(protocol_.make_command("stall_time", times_to_raw(times)));
    }

    void StallManager::set_times(const std::array<float, StallTime::kJointCount> &values) {
        set_times(StallTime::from_array(values));
    }

    void StallManager::set_thresholds(const StallLimit &limits) {
        limits.validate();
        threshold_channel_.command(protocol_.make_command("stall_threshold", limits_to_raw(limits)));
    }

    void StallManager::set_thresholds(const std::array<float, StallLimit::kJointCount> &values) {
        set_thresholds(StallLimit::from_array(values));
    }

    void StallManager::set_torques(const StallLimit &limits) {
        limits.validate();
        torque_channel_.command(protocol_.make_command("stall_torque", limits_to_raw(limits)));
    }

    void StallManager::set_torques(const std::array<float, StallLimit::kJointCount> &values) {
        set_torques(StallLimit::from_array(values));
    }

    StallTimeData StallManager::get_times_blocking(std::chrono::milliseconds timeout_ms) {
        check_timeout(timeout_ms);
        return time_channel_.get(protocol_.make_read("stall_time"), runtime::deadline_after(timeout_ms));
    }

    StallLimitData StallManager::get_thresholds_blocking(std::chrono::milliseconds timeout_ms) {
        check_timeout(timeout_ms);
        return threshold_channel_.get(protocol_.make_read("stall_threshold"), runtime::deadline_after(timeout_ms));
    }

    StallLimitData StallManager::get_torques_blocking(std::chrono::milliseconds timeout_ms) {
        check_timeout(timeout_ms);
        return torque_channel_.get(protocol_.make_read("stall_torque"), runtime::deadline_after(timeout_ms));
    }

    std::optional<StallTimeData> StallManager::get_time_snapshot() const { return time_channel_.snapshot(); }

    std::optional<StallLimitData> StallManager::get_threshold_snapshot() const { return threshold_channel_.snapshot(); }

    std::optional<StallLimitData> StallManager::get_torque_snapshot() const { return torque_channel_.snapshot(); }

    void StallManager::send_time_sense_request() {
        time_channel_.request(protocol_.make_read("stall_time"),
                              runtime::deadline_after(std::chrono::milliseconds(100)));
    }

    void StallManager::send_threshold_sense_request() {
        threshold_channel_.request(protocol_.make_read("stall_threshold"),
                                   runtime::deadline_after(std::chrono::milliseconds(100)));
    }

    void StallManager::send_torque_sense_request() {
        torque_channel_.request(protocol_.make_read("stall_torque"),
                                runtime::deadline_after(std::chrono::milliseconds(100)));
    }

} // namespace linkerbot::hand::o6
