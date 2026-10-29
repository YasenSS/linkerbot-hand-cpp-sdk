#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/capabilities.hpp>
#include <linkerbot/hand/health.hpp>
#include <linkerbot/hand/l20/angle_manager.hpp>
#include <linkerbot/hand/l20/current_manager.hpp>
#include <linkerbot/hand/l20/events.hpp>
#include <linkerbot/hand/l20/fault_manager.hpp>
#include <linkerbot/hand/l20/force_sensor_manager.hpp>
#include <linkerbot/hand/l20/speed_manager.hpp>
#include <linkerbot/hand/l20/temperature_manager.hpp>
#include <linkerbot/hand/l20/version_manager.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/transport/session.hpp>

namespace linkerbot::hand::l20 {

    enum class Side { kLeft, kRight };

    class L20 {
        // Declared first to ensure initialization before managers.
        uint32_t arbitration_id_;
        std::shared_ptr<transport::Session> session_;
        bool owns_session_ = false;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;

    public:
        L20(Side side, std::string_view interface_name,
            const std::optional<std::filesystem::path> &angle_mapping_path = {});
        L20(Side side, std::unique_ptr<can::CANTransport> transport, std::string_view interface_name = "unknown",
            const std::optional<std::filesystem::path> &angle_mapping_path = {});
        L20(Side side, std::shared_ptr<transport::Session> session, std::string_view interface_name = "unknown",
            const std::optional<std::filesystem::path> &angle_mapping_path = {});
        ~L20();

        L20(const L20 &) = delete;
        L20 &operator=(const L20 &) = delete;
        L20(L20 &&) = delete;
        L20 &operator=(L20 &&) = delete;

        [[nodiscard]] std::shared_ptr<runtime::DeviceRuntime> runtime() const { return runtime_; }
        void recover();
        [[nodiscard]] HandHealth health() const { return runtime_->health(); }
        // Compatibility adapter retained throughout 0.2.x; prefer health().
        [[nodiscard]] runtime::RuntimeHealth runtime_health() const { return runtime_->runtime_health(); }

        void stop();

        [[nodiscard]] Snapshot get_snapshot() const;
        [[nodiscard]] const DeviceCapabilities &capabilities() const noexcept;

        EventQueue<SensorEvent> &stream(size_t maxsize = 100);
        void stop_stream();
        std::shared_ptr<EventQueue<SensorEvent>> stream_handle(size_t maxsize = 100);

        void start_polling(const std::vector<SensorSource> &sources = {},
                           std::chrono::milliseconds interval_ms = std::chrono::milliseconds{100});
        void start_polling(const std::map<SensorSource, std::chrono::milliseconds> &intervals);
        void stop_polling();

        // Runtime stops a failed source and retains its diagnostic while other
        // polling sources continue. A new start clears polling diagnostics.
        [[nodiscard]] bool polling_faulted() const;
        [[nodiscard]] std::optional<std::string> last_polling_error() const;

        // Blocks until the runtime reports a faulted state or `timeout`
        // elapses. Returns polling_faulted() at that point, letting callers
        // (tests in particular) react to the exact moment a fault is
        // observable instead of guessing a fixed sleep duration.
        [[nodiscard]] bool wait_for_polling_fault(std::chrono::milliseconds timeout) const;

        AngleManager angle;
        ForceSensorManager force_sensor;
        CurrentManager current;
        CurrentLimitManager current_limit;
        SpeedManager speed;
        TemperatureManager temperature;
        FaultManager fault;
        VersionManager version;

    private:
        void start();
        void push_event(const SensorEvent &event);
        void send_sense_request(SensorSource source);

        std::mutex event_mutex_;
        std::mutex stop_mutex_;

        std::shared_ptr<EventQueue<SensorEvent>> event_queue_;
        std::vector<std::shared_ptr<EventQueue<SensorEvent>>> retired_streams_;
        bool streaming_closed_ = false;
    };

} // namespace linkerbot::hand::l20
