#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l20lite/detail/channel.hpp>
#include <linkerbot/hand/l20lite/force_sensor.hpp>
#include <linkerbot/hand/l20lite/protocol_backend.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/relay.hpp>

namespace linkerbot::hand::l20lite {

    class SingleForceSensorManager {
        friend class L20lite;
        friend struct ManagerTestAccess;
        friend class ForceSensorManager;

    public:
        SingleForceSensorManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher, uint8_t command_prefix);
        SingleForceSensorManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime,
                                 uint8_t command_prefix);
        ~SingleForceSensorManager();

        SingleForceSensorManager(const SingleForceSensorManager &) = delete;
        SingleForceSensorManager &operator=(const SingleForceSensorManager &) = delete;
        SingleForceSensorManager(SingleForceSensorManager &&) = delete;
        SingleForceSensorManager &operator=(SingleForceSensorManager &&) = delete;

        void start();
        void stop();

        [[nodiscard]] ForceSensorData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                       1000});
        [[nodiscard]] std::optional<ForceSensorData> get_snapshot() const;

    private:
        void cancel_pending();
        void send_request();
        void set_event_sink(std::function<void(const ForceSensorData &)> sink);

        uint32_t arbitration_id_;
        std::string operation_;
        uint8_t command_prefix_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        detail::Channel<ForceSensorData> channel_;
    };

    class ForceSensorManager {
        friend class L20lite;
        friend struct ManagerTestAccess;

    public:
        ForceSensorManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        ForceSensorManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~ForceSensorManager();

        ForceSensorManager(const ForceSensorManager &) = delete;
        ForceSensorManager &operator=(const ForceSensorManager &) = delete;
        ForceSensorManager(ForceSensorManager &&) = delete;
        ForceSensorManager &operator=(ForceSensorManager &&) = delete;

        void start();
        void stop();

        [[nodiscard]] AllFingersData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                      1000});
        [[nodiscard]] std::optional<AllFingersData> get_snapshot() const;
        [[nodiscard]] SingleForceSensorManager &get_finger(const std::string &name);

    private:
        std::mutex lifecycle_mutex_;
        void cancel_pending();
        void set_event_sink(std::function<void(const AllFingersData &)> sink);
        void send_sense_request();

        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        std::unordered_map<std::string, std::unique_ptr<SingleForceSensorManager>> fingers_;
        detail::Channel<AllFingersData> channel_;
    };

} // namespace linkerbot::hand::l20lite
