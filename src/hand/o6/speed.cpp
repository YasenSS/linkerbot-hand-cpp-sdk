#include "linkerbot/hand/o6/speed.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/o6/speed_manager.hpp>
#include <optional>
#include <stdexcept>
#include <utility>

namespace linkerbot::hand::o6 {

    namespace {
        std::array<uint8_t, Speed::kJointCount> speeds_to_raw(const Speed &speeds) {
            auto arr = speeds.to_array();
            std::array<uint8_t, Speed::kJointCount> raw{};
            for (size_t i = 0; i < Speed::kJointCount; ++i) {
                raw[i] = static_cast<uint8_t>(arr[i] * 255.0f / 100.0f);
            }
            return raw;
        }
    } // namespace

    SpeedManager::SpeedManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher)
        : SpeedManager(arbitration_id,
                       runtime::DeviceRuntime::borrow(dispatcher, "o6/" + std::to_string(arbitration_id),
                                                      {{arbitration_id, false}})) {}

    SpeedManager::SpeedManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          channel_(runtime_, "speed") {}

    SpeedManager::~SpeedManager() = default;

    void SpeedManager::start() {
        const auto id = arbitration_id_;
        channel_.start([id](const can::Message &message) { return protocol::decode_speed(id, message); });
    }

    void SpeedManager::stop() { channel_.stop(); }

    void SpeedManager::set_speeds(const Speed &speeds) {
        speeds.validate();
        channel_.command(protocol_.make_command("speed", speeds_to_raw(speeds)));
    }

    void SpeedManager::set_speeds(const std::array<float, Speed::kJointCount> &values) {
        auto speeds = Speed::from_array(values);
        set_speeds(speeds);
    }

    SpeedData SpeedManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        return channel_.get(protocol_.make_read("speed"), runtime::deadline_after(timeout_ms));
    }

    std::optional<SpeedData> SpeedManager::get_snapshot() const { return channel_.snapshot(); }

    void SpeedManager::set_event_sink(std::function<void(const SpeedData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

    void SpeedManager::send_sense_request() {
        channel_.request(protocol_.make_read("speed"), runtime::deadline_after(std::chrono::milliseconds(100)));
    }

} // namespace linkerbot::hand::o6
