#include "linkerbot/hand/l20/speed.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20/speed_manager.hpp>
#include <linkerbot/relay.hpp>
#include <optional>
#include <stdexcept>
#include <utility>

namespace linkerbot::hand::l20 {

    SpeedManager::SpeedManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher)
        : SpeedManager(arbitration_id,
                       runtime::DeviceRuntime::borrow(dispatcher, "l20/" + std::to_string(arbitration_id),
                                                      {{arbitration_id, false}})) {}

    SpeedManager::SpeedManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          channel_(runtime_, "speed") {}

    SpeedManager::~SpeedManager() = default;

    void SpeedManager::start() { channel_.start(); }

    void SpeedManager::stop() { channel_.stop(true); }

    void SpeedManager::set_speeds(const Speed &speeds) {
        speeds.validate();
        channel_.request(protocol_.make_command("speed", percents_to_raw(speeds.to_array())), runtime::Deadline::max());
    }

    void SpeedManager::set_speeds(const std::array<float, Speed::kJointCount> &values) {
        set_speeds(Speed::from_array(values));
    }

    SpeedData SpeedManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        const auto deadline = runtime::deadline_after(timeout_ms);
        return channel_.get(protocol_.make_read("speed"), deadline);
    }

    std::optional<SpeedData> SpeedManager::get_snapshot() const { return channel_.snapshot(); }

    void SpeedManager::set_event_sink(std::function<void(const SpeedData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

    void SpeedManager::send_sense_request() {
        channel_.request(protocol_.make_read("speed"), runtime::deadline_after(std::chrono::milliseconds(200)));
    }

} // namespace linkerbot::hand::l20
