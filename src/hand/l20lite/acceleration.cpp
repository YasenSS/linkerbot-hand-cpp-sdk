#include "linkerbot/hand/l20lite/acceleration.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20lite/acceleration_manager.hpp>
#include <linkerbot/relay.hpp>
#include <optional>
#include <stdexcept>
#include <utility>

namespace linkerbot::hand::l20lite {

    AccelerationManager::AccelerationManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher)
        : AccelerationManager(arbitration_id,
                              runtime::DeviceRuntime::borrow(dispatcher, "l20lite/" + std::to_string(arbitration_id),
                                                             {{arbitration_id, false}})) {}

    AccelerationManager::AccelerationManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          channel_(runtime_, "acceleration") {}

    AccelerationManager::~AccelerationManager() = default;

    void AccelerationManager::start() { channel_.start(); }

    void AccelerationManager::stop() { channel_.stop(); }

    void AccelerationManager::set_accelerations(const Acceleration &accelerations) {
        accelerations.validate();
        channel_.command(protocol_.make_command("acceleration", percents_to_accel_raw(accelerations.to_array())));
    }

    void AccelerationManager::set_accelerations(const std::array<float, Acceleration::kJointCount> &values) {
        set_accelerations(Acceleration::from_array(values));
    }

    AccelerationData AccelerationManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        const auto deadline = runtime::deadline_after(timeout_ms);
        return channel_.get(protocol_.make_read("acceleration"), deadline);
    }

    std::optional<AccelerationData> AccelerationManager::get_snapshot() const { return channel_.snapshot(); }

    void AccelerationManager::set_event_sink(std::function<void(const AccelerationData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

    void AccelerationManager::send_sense_request() {
        channel_.request(protocol_.make_read("acceleration"), runtime::deadline_after(std::chrono::milliseconds(100)));
    }

} // namespace linkerbot::hand::l20lite
