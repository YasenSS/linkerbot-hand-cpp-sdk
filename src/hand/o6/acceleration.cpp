#include "linkerbot/hand/o6/acceleration.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/o6/acceleration_manager.hpp>
#include <optional>
#include <stdexcept>
#include <utility>

namespace linkerbot::hand::o6 {

    namespace {
        // Hardware: 0 = max, 1–254 increasing. User 100 → 0; user 0–99 → 1–254.
        uint8_t percent_to_raw(float percent) {
            if (percent >= 100.0f) {
                return 0;
            }
            const auto rounded = std::lround(1.0 + static_cast<double>(percent) * 253.0 / 99.0);
            if (rounded < 1) {
                return 1;
            }
            if (rounded > 254) {
                return 254;
            }
            return static_cast<uint8_t>(rounded);
        }

        std::array<uint8_t, Acceleration::kJointCount> accelerations_to_raw(const Acceleration &accelerations) {
            auto arr = accelerations.to_array();
            std::array<uint8_t, Acceleration::kJointCount> raw{};
            for (size_t i = 0; i < Acceleration::kJointCount; ++i) {
                raw[i] = percent_to_raw(arr[i]);
            }
            return raw;
        }
    } // namespace

    AccelerationManager::AccelerationManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher)
        : AccelerationManager(arbitration_id,
                              runtime::DeviceRuntime::borrow(dispatcher, "o6/" + std::to_string(arbitration_id),
                                                             {{arbitration_id, false}})) {}

    AccelerationManager::AccelerationManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          channel_(runtime_, "acceleration") {}

    AccelerationManager::~AccelerationManager() = default;

    void AccelerationManager::start() {
        const auto id = arbitration_id_;
        channel_.start([id](const can::Message &message) { return protocol::decode_acceleration(id, message); });
    }

    void AccelerationManager::stop() { channel_.stop(); }

    void AccelerationManager::set_accelerations(const Acceleration &accelerations) {
        accelerations.validate();
        channel_.command(protocol_.make_command("acceleration", accelerations_to_raw(accelerations)));
    }

    void AccelerationManager::set_accelerations(const std::array<float, Acceleration::kJointCount> &values) {
        auto accelerations = Acceleration::from_array(values);
        set_accelerations(accelerations);
    }

    AccelerationData AccelerationManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        return channel_.get(protocol_.make_read("acceleration"), runtime::deadline_after(timeout_ms));
    }

    std::optional<AccelerationData> AccelerationManager::get_snapshot() const { return channel_.snapshot(); }

    void AccelerationManager::set_event_sink(std::function<void(const AccelerationData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

    void AccelerationManager::send_sense_request() {
        channel_.request(protocol_.make_read("acceleration"), runtime::deadline_after(std::chrono::milliseconds(100)));
    }

} // namespace linkerbot::hand::o6
