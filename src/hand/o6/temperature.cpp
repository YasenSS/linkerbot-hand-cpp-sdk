#include "linkerbot/hand/o6/temperature.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/o6/temperature_manager.hpp>
#include <optional>
#include <stdexcept>
#include <utility>

namespace linkerbot::hand::o6 {

    TemperatureManager::TemperatureManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher)
        : TemperatureManager(arbitration_id,
                             runtime::DeviceRuntime::borrow(dispatcher, "o6/" + std::to_string(arbitration_id),
                                                            {{arbitration_id, false}})) {}

    TemperatureManager::TemperatureManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          channel_(runtime_, "temperature") {}

    TemperatureManager::~TemperatureManager() = default;

    void TemperatureManager::start() {
        const auto id = arbitration_id_;
        channel_.start([id](const can::Message &message) { return protocol::decode_temperature(id, message); });
    }

    void TemperatureManager::stop() { channel_.stop(); }

    TemperatureData TemperatureManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        return channel_.get(protocol_.make_read("temperature"), runtime::deadline_after(timeout_ms));
    }

    std::optional<TemperatureData> TemperatureManager::get_snapshot() const { return channel_.snapshot(); }

    void TemperatureManager::set_event_sink(std::function<void(const TemperatureData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

    void TemperatureManager::send_sense_request() {
        channel_.request(protocol_.make_read("temperature"), runtime::deadline_after(std::chrono::milliseconds(100)));
    }

} // namespace linkerbot::hand::o6
