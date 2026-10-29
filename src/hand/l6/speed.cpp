#include "linkerbot/hand/l6/speed.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <linkerbot/hand/l6/detail/channel.hpp>
#include <linkerbot/hand/l6/protocol_backend.hpp>
#include <linkerbot/hand/l6/speed_manager.hpp>

namespace linkerbot::hand::l6 {

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
        : SpeedManager(arbitration_id, detail::borrow_runtime(dispatcher, arbitration_id)) {}

    SpeedManager::SpeedManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), channel_(std::move(runtime), "speed") {}

    void SpeedManager::start() {
        channel_.start();
        const auto id = arbitration_id_;
        channel_.observe(
            [id](const can::Message &message) { return protocol::raw_control_reply(id, kSpeedOpcode, message, true); });
    }

    void SpeedManager::set_raw_speeds(const std::array<uint8_t, Speed::kJointCount> &values,
                                      std::chrono::milliseconds timeout) {
        const auto call = channel_.enter();
        static_cast<void>(
            channel_.get(protocol::raw_control_transaction(arbitration_id_, kSpeedOpcode, "speed", values),
                         runtime::deadline_after(timeout)));
    }

    std::optional<RawControlData> SpeedManager::get_snapshot() const { return channel_.snapshot(); }

    void SpeedManager::set_speeds(const Speed &speeds) {
        const auto call = channel_.enter();
        speeds.validate();
        auto raw = speeds_to_raw(speeds);
        channel_.command(protocol::command(arbitration_id_, kSpeedOpcode, {raw.begin(), raw.end()}));
    }

    void SpeedManager::set_speeds(const std::array<float, Speed::kJointCount> &values) {
        const auto call = channel_.enter();
        auto speeds = Speed::from_array(values);
        set_speeds(speeds);
    }

} // namespace linkerbot::hand::l6
