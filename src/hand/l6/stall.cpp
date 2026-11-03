#include "linkerbot/hand/l6/stall.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l6/frame.hpp>
#include <linkerbot/hand/l6/protocol_backend.hpp>
#include <linkerbot/hand/l6/stall_manager.hpp>
#include <stdexcept>

namespace linkerbot::hand::l6 {

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

        float raw_to_ms(uint8_t raw) { return static_cast<float>(raw) * 10.0f; }

        uint8_t limit_to_raw(float value) {
            const auto rounded = std::lround(static_cast<double>(value) * 255.0 / 1400.0);
            if (rounded < 0) {
                return 0;
            }
            if (rounded > 255) {
                return 255;
            }
            return static_cast<uint8_t>(rounded);
        }

        float raw_to_limit(uint8_t raw) { return static_cast<float>(raw) * 1400.0f / 255.0f; }

        std::array<uint8_t, StallTime::kJointCount> times_to_raw(const StallTime &times) {
            auto arr = times.to_array();
            std::array<uint8_t, StallTime::kJointCount> raw{};
            for (size_t i = 0; i < StallTime::kJointCount; ++i) {
                raw[i] = ms_to_raw(arr[i]);
                if (raw[i] == 0) {
                    throw ValidationError("L6 stall time write must encode to raw 1..255; raw 0 has no effect");
                }
            }
            return raw;
        }

        StallTime times_from_raw(const std::array<uint8_t, StallTime::kJointCount> &raw) {
            std::array<float, StallTime::kJointCount> values{};
            for (size_t i = 0; i < StallTime::kJointCount; ++i) {
                values[i] = raw_to_ms(raw[i]);
            }
            return StallTime::from_array(values);
        }

        std::array<uint8_t, StallLimit::kJointCount> limits_to_raw(const StallLimit &limits) {
            auto arr = limits.to_array();
            std::array<uint8_t, StallLimit::kJointCount> raw{};
            for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
                raw[i] = limit_to_raw(arr[i]);
            }
            return raw;
        }

        StallLimit limits_from_raw(const std::array<uint8_t, StallLimit::kJointCount> &raw) {
            std::array<float, StallLimit::kJointCount> values{};
            for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
                values[i] = raw_to_limit(raw[i]);
            }
            return StallLimit::from_array(values);
        }

        std::array<uint8_t, StallTime::kJointCount> read_joint_raw(const can::Message &msg) {
            std::array<uint8_t, StallTime::kJointCount> raw{};
            for (size_t i = 0; i < StallTime::kJointCount; ++i) {
                raw[i] = msg.data[i + 1];
            }
            return raw;
        }

        float wait_or_throw(std::chrono::milliseconds timeout_ms) {
            if (timeout_ms.count() <= 0) {
                throw ValidationError("timeout_ms must be positive");
            }
            return std::chrono::duration_cast<std::chrono::duration<float>>(timeout_ms).count();
        }
    } // namespace

    StallManager::StallManager(uint32_t id, can::CANDispatcher &dispatcher)
        : StallManager(id, detail::borrow_runtime(dispatcher, id)) {}
    StallManager::StallManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), time_(runtime, "stall.time"), threshold_(runtime, "stall.threshold"),
          torque_(runtime, "stall.torque") {}
    StallManager::~StallManager() {
        try {
            stop();
            // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
            // NOLINTNEXTLINE(bugprone-empty-catch)
        } catch (...) {
        }
        time_.drain_calls();
        threshold_.drain_calls();
        torque_.drain_calls();
    }
    void StallManager::start() {
        time_.start();
        threshold_.start();
        torque_.start();
        const auto id = arbitration_id_;
        time_.observe([id](const can::Message &msg) {
            return protocol::stall(id, kTimeCmd, msg, protocol::common::Origin::unknown);
        });
        threshold_.observe([id](const can::Message &msg) {
            return protocol::stall(id, kThresholdCmd, msg, protocol::common::Origin::unknown);
        });
        torque_.observe([id](const can::Message &msg) {
            return protocol::stall(id, kTorqueCmd, msg, protocol::common::Origin::unknown);
        });
    }
    void StallManager::stop() {
        time_.cancel();
        threshold_.cancel();
        torque_.cancel();
        time_.stop();
        threshold_.stop();
        torque_.stop();
    }

    void StallManager::set_times(const StallTime &times) {
        const auto call = time_.enter();
        times.validate();
        send_joint_frame(kTimeCmd, times_to_raw(times));
    }

    void StallManager::set_times(const std::array<float, StallTime::kJointCount> &values) {
        const auto call = time_.enter();
        set_times(StallTime::from_array(values));
    }

    void StallManager::set_thresholds(const StallLimit &limits) {
        const auto call = time_.enter();
        limits.validate();
        send_joint_frame(kThresholdCmd, limits_to_raw(limits));
    }

    void StallManager::set_thresholds(const std::array<float, StallLimit::kJointCount> &values) {
        const auto call = time_.enter();
        set_thresholds(StallLimit::from_array(values));
    }

    void StallManager::set_torques(const StallLimit &limits) {
        const auto call = time_.enter();
        limits.validate();
        send_joint_frame(kTorqueCmd, limits_to_raw(limits));
    }

    void StallManager::set_torques(const std::array<float, StallLimit::kJointCount> &values) {
        const auto call = time_.enter();
        set_torques(StallLimit::from_array(values));
    }

    StallTimeData StallManager::get_times_blocking(std::chrono::milliseconds timeout_ms) {
        const auto call = time_.enter();
        const auto deadline = runtime::deadline_after(timeout_ms);
        return time_.get(protocol::read_stall(arbitration_id_, kTimeCmd), deadline);
    }

    StallLimitData StallManager::get_thresholds_blocking(std::chrono::milliseconds timeout_ms) {
        const auto call = time_.enter();
        const auto deadline = runtime::deadline_after(timeout_ms);
        return threshold_.get(protocol::read_stall(arbitration_id_, kThresholdCmd), deadline);
    }

    StallLimitData StallManager::get_torques_blocking(std::chrono::milliseconds timeout_ms) {
        const auto call = time_.enter();
        const auto deadline = runtime::deadline_after(timeout_ms);
        return torque_.get(protocol::read_stall(arbitration_id_, kTorqueCmd), deadline);
    }

    std::optional<StallTimeData> StallManager::get_time_snapshot() const { return time_.snapshot(); }

    std::optional<StallLimitData> StallManager::get_threshold_snapshot() const { return threshold_.snapshot(); }

    std::optional<StallLimitData> StallManager::get_torque_snapshot() const { return torque_.snapshot(); }

    void StallManager::send_joint_frame(uint8_t cmd, const std::array<uint8_t, StallTime::kJointCount> &raw) {
        const auto call = time_.enter();
        auto plan = protocol::command(arbitration_id_, cmd, {raw.begin(), raw.end()});
        if (cmd == kTimeCmd) {
            time_.command(std::move(plan));
        } else if (cmd == kThresholdCmd) {
            threshold_.command(std::move(plan));
        } else {
            torque_.command(std::move(plan));
        }
    }

    void StallManager::send_sense_request(uint8_t cmd) {
        const auto call = time_.enter();
        auto plan = protocol::read_stall(arbitration_id_, cmd);
        const auto deadline = runtime::deadline_after(std::chrono::milliseconds(100));
        if (cmd == kTimeCmd) {
            time_.request(std::move(plan), deadline);
        } else if (cmd == kThresholdCmd) {
            threshold_.request(std::move(plan), deadline);
        } else {
            torque_.request(std::move(plan), deadline);
        }
    }
} // namespace linkerbot::hand::l6
