#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/capabilities.hpp>
#include <linkerbot/hand/health.hpp>
#include <linkerbot/hand/l30/angle_manager.hpp>
#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/l30/control_manager.hpp>
#include <linkerbot/hand/l30/current_limit_manager.hpp>
#include <linkerbot/hand/l30/current_manager.hpp>
#include <linkerbot/hand/l30/events.hpp>
#include <linkerbot/hand/l30/fault_manager.hpp>
#include <linkerbot/hand/l30/force_sensor_manager.hpp>
#include <linkerbot/hand/l30/report_manager.hpp>
#include <linkerbot/hand/l30/speed_manager.hpp>
#include <linkerbot/hand/l30/temperature_manager.hpp>
#include <linkerbot/hand/l30/version_manager.hpp>

namespace linkerbot::hand::l30 {

    // Default node/host addressing. The device answers on NodeID 1 out of the
    // factory; the host may pick any SrcID, and 0 keeps requests distinguishable
    // from other nodes on the bus.
    inline constexpr uint8_t kDefaultNodeId = 1;
    inline constexpr uint8_t kDefaultHostId = 0;

    // Angle polls default to 60 Hz, matching the Python SDK.
    inline constexpr std::chrono::milliseconds kDefaultPollInterval{17};

    // SDK entry point for one L30 hand.
    //
    // L30 speaks CAN FD extended frames, so the interface must be up in FD mode
    // before construction.
    //
    // The hand owns the dispatcher unless one is supplied. Managers are public
    // members; every one of them talks through the single shared Client.
    class L30 {
        // Declared before the managers so they are constructed first.
        // Session ownership controls physical shutdown; borrowed/shared sessions survive stop().
        std::shared_ptr<transport::Session> session_;
        bool owns_session_ = false;
        std::unique_ptr<Client> client_;

    public:
        // Opens a SocketCAN FD transport on interface_name.
        explicit L30(std::string_view interface_name, uint8_t node_id = kDefaultNodeId,
                     uint8_t host_id = kDefaultHostId, bool auto_start_periodic = true);

        // Takes an already-built transport (a FakeTransport in tests, or a
        // vendor CAN FD adapter).
        L30(std::unique_ptr<can::CANTransport> transport, uint8_t node_id = kDefaultNodeId,
            uint8_t host_id = kDefaultHostId, bool auto_start_periodic = true);

        // Shares a dispatcher this object does not own — several L30 hands can
        // sit on one CAN FD bus as long as each has a distinct NodeID. The
        // dispatcher must outlive every hand attached to it, and stop() leaves
        // it running.
        L30(can::CANDispatcher &dispatcher, uint8_t node_id = kDefaultNodeId, uint8_t host_id = kDefaultHostId,
            bool auto_start_periodic = true);

        L30(std::shared_ptr<transport::Session> session, uint8_t node_id = kDefaultNodeId,
            uint8_t host_id = kDefaultHostId, bool auto_start_periodic = true);
        L30(std::shared_ptr<runtime::DeviceRuntime> core, uint8_t node_id = kDefaultNodeId,
            uint8_t host_id = kDefaultHostId, bool auto_start_periodic = true);
        [[nodiscard]] std::shared_ptr<runtime::DeviceRuntime> runtime() const { return client_->runtime(); }
        [[nodiscard]] HandHealth health() const { return runtime()->health(); }
        // Compatibility adapter for older callers; prefer health().
        [[nodiscard]] runtime::RuntimeHealth runtime_health() const { return runtime()->runtime_health(); }
        ~L30();

        L30(const L30 &) = delete;
        L30 &operator=(const L30 &) = delete;
        L30(L30 &&) = delete;
        L30 &operator=(L30 &&) = delete;

        [[nodiscard]] uint8_t node_id() const;
        [[nodiscard]] uint8_t host_id() const;

        // Stops streaming, polling and periodic reports, then closes the client
        // and the owned dispatcher. Idempotent; the destructor calls it.
        void stop();

        // Non-blocking aggregate of every manager's cache.
        [[nodiscard]] Snapshot get_snapshot() const;
        [[nodiscard]] const DeviceCapabilities &capabilities() const noexcept;

        EventQueue<SensorEvent> &stream(size_t maxsize = 100);
        void stop_stream();

        // Device-side reporting: the hand pushes on its own schedule.
        void start_periodic_reports(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void stop_periodic_reports(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        // Host-side polling: Runtime schedules cancellable reads for each source. An
        // empty map polls angles at kDefaultPollInterval. This is an alternative
        // to periodic reports, not a complement — both feed the same snapshots.
        void start_polling(const std::map<SensorSource, std::chrono::milliseconds> &intervals = {});
        void stop_polling();
        // Last poll failure for this source; quarantined keys stop their worker
        // and require explicit device/transport recovery and reconstruction.
        [[nodiscard]] std::exception_ptr polling_error(SensorSource source) const;

        ControlManager control;
        AngleManager angle;
        SpeedManager speed;
        CurrentLimitManager current_limit;
        CurrentManager current;
        TemperatureManager temperature;
        FaultManager fault;
        ForceSensorManager force_sensor;
        ReportManager report;
        VersionManager version;

    private:
        void start(bool auto_start_periodic);
        void push_event(const SensorEvent &event);
        void stop_polling_locked();
        void send_sense_request(SensorSource source);

        std::mutex event_mutex_;
        std::mutex polling_mutex_;

        std::unique_ptr<EventQueue<SensorEvent>> event_queue_;
        // Legacy stream() references remain valid until this hand is destroyed.
        std::vector<std::unique_ptr<EventQueue<SensorEvent>>> retired_event_queues_;
        std::atomic<bool> stopped_{false};
        std::mutex stop_mutex_;
        mutable std::mutex polling_wait_mutex_;
        std::map<SensorSource, std::exception_ptr> polling_errors_;
    };

} // namespace linkerbot::hand::l30
