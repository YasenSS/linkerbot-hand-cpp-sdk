#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/capabilities.hpp>
#include <linkerbot/hand/health.hpp>
#include <linkerbot/hand/o30/acceleration_manager.hpp>
#include <linkerbot/hand/o30/angle_manager.hpp>
#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/comm_error_manager.hpp>
#include <linkerbot/hand/o30/config_manager.hpp>
#include <linkerbot/hand/o30/current_manager.hpp>
#include <linkerbot/hand/o30/enable_manager.hpp>
#include <linkerbot/hand/o30/events.hpp>
#include <linkerbot/hand/o30/fault_manager.hpp>
#include <linkerbot/hand/o30/mapping_manager.hpp>
#include <linkerbot/hand/o30/pid_manager.hpp>
#include <linkerbot/hand/o30/preset_manager.hpp>
#include <linkerbot/hand/o30/sensor_manager.hpp>
#include <linkerbot/hand/o30/service_manager.hpp>
#include <linkerbot/hand/o30/speed_manager.hpp>
#include <linkerbot/hand/o30/stall_manager.hpp>
#include <linkerbot/hand/o30/temperature_manager.hpp>
#include <linkerbot/hand/o30/torque_manager.hpp>
#include <linkerbot/hand/o30/unit_manager.hpp>
#include <linkerbot/hand/o30/version_manager.hpp>
#include <linkerbot/hand/o30/voltage_manager.hpp>

namespace linkerbot::hand::o30 {

    inline constexpr std::chrono::milliseconds kDefaultPollInterval{100};

    // SDK entry point for one O30 hand.
    //
    // O30 speaks HOP over CAN FD standard frames (request 0x001, reply 0x401
    // by default, no BRS). The interface must already be up in FD mode.
    class O30 {
        std::shared_ptr<transport::Session> session_;
        bool owns_session_ = false;
        std::unique_ptr<Client> client_;

    public:
        explicit O30(std::string_view interface_name, uint32_t request_id = protocol::kDefaultRequestId);

        O30(std::unique_ptr<can::CANTransport> transport, uint32_t request_id = protocol::kDefaultRequestId);

        // Shares a dispatcher this object does not own. The dispatcher must
        // outlive the hand; stop() leaves it running.
        O30(can::CANDispatcher &dispatcher, uint32_t request_id = protocol::kDefaultRequestId);

        O30(std::shared_ptr<transport::Session> session, uint32_t request_id = protocol::kDefaultRequestId);
        ~O30();
        [[nodiscard]] std::shared_ptr<runtime::DeviceRuntime> runtime() const { return client_->runtime(); }
        void recover();
        [[nodiscard]] HandHealth health() const { return runtime()->health(); }
        // Compatibility adapter for older callers; prefer health().
        [[nodiscard]] runtime::RuntimeHealth runtime_health() const { return runtime()->runtime_health(); }
        std::shared_ptr<EventQueue<SensorEvent>> stream_handle(size_t maxsize = 100);

        O30(const O30 &) = delete;
        O30 &operator=(const O30 &) = delete;
        O30(O30 &&) = delete;
        O30 &operator=(O30 &&) = delete;

        [[nodiscard]] uint32_t request_id() const;
        [[nodiscard]] uint32_t response_id() const;

        void stop();

        [[nodiscard]] Snapshot get_snapshot() const;
        [[nodiscard]] const DeviceCapabilities &capabilities() const noexcept;

        EventQueue<SensorEvent> &stream(size_t maxsize = 100);
        void stop_stream();

        void start_polling(const std::map<SensorSource, std::chrono::milliseconds> &intervals = {});
        void stop_polling();

        MappingManager mapping;
        AngleManager angle;
        SpeedManager speed;
        AccelerationManager acceleration;
        CurrentManager current;
        VoltageManager voltage;
        TorqueManager torque;
        TemperatureManager temperature;
        StallManager stall;
        EnableManager enable;
        FaultManager fault;
        PidManager pid;
        SensorManager sensor;
        ConfigManager config;
        PresetManager preset;
        VersionManager version;
        ServiceManager service;
        UnitManager unit;
        CommErrorManager comm_error;

    private:
        void start();
        void push_event(const SensorEvent &event);
        void send_sense_request(SensorSource source);

        std::mutex event_mutex_;

        std::shared_ptr<EventQueue<SensorEvent>> event_queue_;
        std::vector<std::shared_ptr<EventQueue<SensorEvent>>> retired_streams_;
        std::mutex stop_mutex_;
    };

} // namespace linkerbot::hand::o30
