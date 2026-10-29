#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/capabilities.hpp>
#include <linkerbot/hand/health.hpp>
#include <linkerbot/hand/l6/angle_manager.hpp>
#include <linkerbot/hand/l6/config_manager.hpp>
#include <linkerbot/hand/l6/current_limit_manager.hpp>
#include <linkerbot/hand/l6/current_manager.hpp>
#include <linkerbot/hand/l6/events.hpp>
#include <linkerbot/hand/l6/fault_manager.hpp>
#include <linkerbot/hand/l6/force_sensor_manager.hpp>
#include <linkerbot/hand/l6/raw_control_manager.hpp>
#include <linkerbot/hand/l6/runtime.hpp>
#include <linkerbot/hand/l6/side.hpp>
#include <linkerbot/hand/l6/speed_manager.hpp>
#include <linkerbot/hand/l6/stall_manager.hpp>
#include <linkerbot/hand/l6/temperature_manager.hpp>
#include <linkerbot/hand/l6/torque_manager.hpp>
#include <linkerbot/hand/l6/version_manager.hpp>

namespace linkerbot::hand::l6 {

    class L6 {
        // Declared first to ensure initialization before managers.
        uint32_t arbitration_id_;
        std::shared_ptr<transport::Session> session_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        bool owns_session_ = false;

    public:
        L6(Side side, std::string_view interface_name,
           const std::optional<std::filesystem::path> &angle_mapping_path = {});
        L6(Side side, std::unique_ptr<can::CANTransport> transport, std::string_view interface_name = "unknown",
           const std::optional<std::filesystem::path> &angle_mapping_path = {});
        L6(Side side, std::shared_ptr<transport::Session> session, std::string_view interface_name = "unknown",
           const std::optional<std::filesystem::path> &angle_mapping_path = {});
        ~L6();
        std::shared_ptr<runtime::DeviceRuntime> runtime() const { return runtime_; }
        [[nodiscard]] HandHealth health() const { return runtime_->health(); }
        // Compatibility adapter retained throughout 0.2.x; prefer health().
        [[nodiscard]] runtime::RuntimeHealth runtime_health() const { return runtime_->runtime_health(); }
        void recover();
        std::shared_ptr<EventQueue<SensorEvent>> stream_handle(size_t maxsize = 100);

        L6(const L6 &) = delete;
        L6 &operator=(const L6 &) = delete;
        L6(L6 &&) = delete;
        L6 &operator=(L6 &&) = delete;

        void stop();

        [[nodiscard]] Snapshot get_snapshot() const;
        [[nodiscard]] const DeviceCapabilities &capabilities() const noexcept;

        EventQueue<SensorEvent> &stream(size_t maxsize = 100);
        void stop_stream();

        void start_polling(const std::vector<SensorSource> &sources = {},
                           std::chrono::milliseconds interval_ms = std::chrono::milliseconds{100});
        void start_polling(const L6PollingPlan &plan);
        void stop_polling();
        // Stops admitting polling work, lets the current wire transaction
        // finish up to timeout, then falls back to forced cancellation.
        [[nodiscard]] bool stop_polling_gracefully(std::chrono::milliseconds timeout);

        [[nodiscard]] RuntimeStatsSnapshot get_runtime_stats() const;
        [[nodiscard]] can::TransportHealth get_transport_health() const;
        void reset_runtime_stats();

        AngleManager angle;
        ForceSensorManager force_sensor;
        CurrentLimitManager current_limit;
        TorqueManager torque;
        SpeedManager speed;
        TemperatureManager temperature;
        CurrentManager current;
        FaultManager fault;
        VersionManager version;
        StallManager stall;
        ConfigManager config;
        RawControlManager position_kp;
        RawControlManager position_kd;
        RawControlManager torque_kd;
        RawControlManager limit_compensation;

    private:
        void drain_manager_calls();
        struct PollChannel {
            bool enabled = false;
            std::chrono::microseconds period{100000};
            std::chrono::steady_clock::time_point next{};
        };

        void start();
        void push_event(const SensorEvent &event);
        void scheduler_step();
        bool polling_allowed() const;
        bool take_angle_due_locked(std::chrono::steady_clock::time_point now,
                                   std::chrono::steady_clock::time_point &due);
        void query_scheduled_angle(std::chrono::steady_clock::time_point due, runtime::Deadline deadline);
        void before_tactile_finger(size_t index, std::chrono::steady_clock::time_point deadline);
        void stop_polling_locked();
        L6PollingPlan make_plan_from_sources(const std::vector<SensorSource> &sources,
                                             std::chrono::milliseconds interval_ms) const;
        void apply_plan_locked(const L6PollingPlan &plan);
        void on_command_feedback();
        std::optional<std::chrono::steady_clock::time_point> next_deadline_locked() const;
        bool take_due_locked(PollChannel &channel, std::chrono::steady_clock::time_point now);

        mutable std::mutex event_mutex_;
        std::mutex polling_mutex_;
        std::mutex scheduler_mutex_;
        std::mutex stop_mutex_;
        mutable std::mutex stats_mutex_;
        runtime::RuntimeHealth stats_baseline_;
        uint64_t angle_sent_baseline_ = 0, tactile_sent_baseline_ = 0;

        std::atomic<bool> polling_active_{false};
        RuntimeStats stats_;
        L6PollingPlan plan_{};
        PollChannel angle_channel_;
        PollChannel tactile_channel_;
        PollChannel current_limit_channel_;
        PollChannel torque_channel_;
        PollChannel temperature_channel_;
        PollChannel current_channel_;
        PollChannel fault_channel_;

        std::shared_ptr<EventQueue<SensorEvent>> event_queue_;
        std::vector<std::shared_ptr<EventQueue<SensorEvent>>> retired_streams_;
    };

} // namespace linkerbot::hand::l6
