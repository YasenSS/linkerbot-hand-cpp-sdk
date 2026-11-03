#include <chrono>
#include <cstddef>
#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l30/events.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef __linux__
#include <linkerbot/can/socketcan_transport.hpp>
#endif

namespace linkerbot::hand::l30 {

    namespace {
        std::unique_ptr<can::CANTransport> make_transport(std::string_view interface_name) {
#ifdef __linux__
            // fd_mode=true: L30 only speaks CAN FD.
            return std::make_unique<can::SocketCANTransport>(interface_name, true);
#else
            (void)interface_name;
            throw TransportError("SocketCAN FD is only supported on Linux. "
                                 "Construct L30 with a custom CANTransport on other platforms.");
#endif
        }
    } // namespace

    std::string_view sensor_source_name(SensorSource source) {
        switch (source) {
        case SensorSource::kAngle:
            return "angle";
        case SensorSource::kSpeed:
            return "speed";
        case SensorSource::kCurrent:
            return "current";
        case SensorSource::kTemperature:
            return "temperature";
        case SensorSource::kFault:
            return "fault";
        case SensorSource::kForceSensor:
            return "force_sensor";
        }
        throw ValidationError("unknown L30 sensor source");
    }

    L30::L30(std::string_view interface_name, uint8_t node_id, uint8_t host_id, bool auto_start_periodic)
        : L30(make_transport(interface_name), node_id, host_id, auto_start_periodic) {}

    L30::L30(std::unique_ptr<can::CANTransport> transport, uint8_t node_id, uint8_t host_id, bool auto_start_periodic)
        : L30(transport::Session::own(std::move(transport)), node_id, host_id, auto_start_periodic) {
        owns_session_ = true;
    }

    L30::L30(can::CANDispatcher &dispatcher, uint8_t node_id, uint8_t host_id, bool auto_start_periodic)
        : L30(transport::Session::borrow(dispatcher), node_id, host_id, auto_start_periodic) {}

    L30::L30(std::shared_ptr<transport::Session> session, uint8_t node_id, uint8_t host_id, bool auto_start_periodic)
        : session_(std::move(session)), client_(std::make_unique<Client>(session_, node_id, host_id)),
          control(*client_), angle(*client_), speed(*client_), current_limit(*client_), current(*client_),
          temperature(*client_), fault(*client_), force_sensor(*client_), report(*client_), version(*client_) {
        for (const auto *channel : {"angles", "speeds", "currents", "force", "temperatures", "faults", "version"}) {
            runtime()->register_channel(channel);
        }
        runtime()->register_channel("stall", 0.0, false);
        for (const auto *family :
             {"angle_targets", "speed_targets", "current_limits", "fault_reset", "configuration", "l30_writes"}) {
            runtime()->register_command_family(family);
        }
        start(auto_start_periodic);
    }

    L30::L30(std::shared_ptr<runtime::DeviceRuntime> core, uint8_t node_id, uint8_t host_id, bool auto_start_periodic)
        : session_(core ? core->session() : nullptr),
          client_(std::make_unique<Client>(std::move(core), node_id, host_id)), control(*client_), angle(*client_),
          speed(*client_), current_limit(*client_), current(*client_), temperature(*client_), fault(*client_),
          force_sensor(*client_), report(*client_), version(*client_) {
        for (const auto *channel : {"angles", "speeds", "currents", "force", "temperatures", "faults", "version"}) {
            runtime()->register_channel(channel);
        }
        runtime()->register_channel("stall", 0.0, false);
        for (const auto *family :
             {"angle_targets", "speed_targets", "current_limits", "fault_reset", "configuration", "l30_writes"}) {
            runtime()->register_command_family(family);
        }
        start(auto_start_periodic);
    }

    L30::~L30() {
        try {
            stop();
        } catch (...) // Destructor may run on a Runtime event callback.
        {
            try {
                client_->begin_shutdown();
                stop_stream();
                runtime()->quiesce_for_destruction();
                client_->drain_calls();
                // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
                // NOLINTNEXTLINE(bugprone-empty-catch)
            } catch (...) {
            }
        }
    }

    uint8_t L30::node_id() const { return client_->node_id(); }

    uint8_t L30::host_id() const { return client_->host_id(); }

    const DeviceCapabilities &L30::capabilities() const noexcept {
        return declared_capabilities(Model::kL30, linkerbot::hand::HandSide::kUnknown);
    }

    void L30::start(bool auto_start_periodic) {
        angle.set_event_sink([this](const AngleData &d) { push_event(AngleEvent{d}); });
        speed.set_event_sink([this](const SpeedData &d) { push_event(SpeedEvent{d}); });
        current_limit.set_event_sink([this](const CurrentLimitData &d) { push_event(CurrentLimitEvent{d}); });
        current.set_event_sink([this](const CurrentData &d) { push_event(CurrentEvent{d}); });
        temperature.set_event_sink([this](const TemperatureData &d) { push_event(TemperatureEvent{d}); });
        fault.set_event_sink([this](const FaultData &d) { push_event(FaultEvent{d}); });
        force_sensor.set_event_sink([this](const AllFingersData &d) { push_event(ForceSensorEvent{d}); });

        if (auto_start_periodic) {
            start_periodic_reports();
        }
    }

    void L30::stop() {
        // Reject before lifecycle locks: these callers cannot synchronously
        // join the thread whose callback they are currently executing.
        runtime()->assert_external_lifecycle();
        const std::lock_guard<std::mutex> stop_lock(stop_mutex_);
        if (stopped_) {
            // A prior backend close failure retains physical ownership.
            if (owns_session_) {
                session_->close();
            }
            return;
        }
        stopped_ = true;
        std::exception_ptr first_error;
        const auto stage = [&](auto &&fn) {
            try {
                fn();
            } catch (...) {
                if (!first_error) {
                    first_error = std::current_exception();
                }
            }
        };
        stage([&] { client_->begin_shutdown(); });
        stage([&] { stop_stream(); });
        stage([&] { stop_polling(); });
        // Device-side reports are best effort on unplugged hardware. Failure
        // must not bypass local subscription, waiter or backend cleanup.
        try {
            report.disable_all();
        } catch (...) {
        }
        stage([&] { angle.close(); });
        stage([&] { speed.close(); });
        stage([&] { current.close(); });
        stage([&] { temperature.close(); });
        stage([&] { fault.close(); });
        stage([&] { current_limit.relay_.close(); });
        stage([&] { force_sensor.relay_.close(); });
        stage([&] { client_->close(); });
        if (owns_session_) {
            stage([&] { session_->close(); });
        }
        if (first_error) {
            std::rethrow_exception(first_error);
        }
    }

    Snapshot L30::get_snapshot() const {
        return Snapshot{
            angle.get_snapshot(),
            speed.get_snapshot(),
            current_limit.get_snapshot(),
            current.get_snapshot(),
            temperature.get_snapshot(),
            fault.get_snapshot(),
            force_sensor.get_snapshot(),
            std::chrono::steady_clock::now(),
            speed.get_configured_snapshot(),
        };
    }

    EventQueue<SensorEvent> &L30::stream(size_t maxsize) {
        const std::scoped_lock lock(event_mutex_);
        if (stopped_) {
            throw ClosedError("L30 is stopped");
        }
        if (event_queue_ && !event_queue_->closed()) {
            throw LifecycleError("stream() already active; call stop_stream() first");
        }
        auto next = std::make_unique<EventQueue<SensorEvent>>(maxsize);
        if (event_queue_) {
            retired_event_queues_.push_back(std::move(event_queue_));
        }
        event_queue_ = std::move(next);
        return *event_queue_;
    }

    void L30::stop_stream() {
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_) {
            event_queue_->close();
        }
    }

    void L30::start_periodic_reports(std::chrono::milliseconds timeout_ms) { report.start_default(timeout_ms); }

    void L30::stop_periodic_reports(std::chrono::milliseconds timeout_ms) { report.disable_all(timeout_ms); }

    void L30::start_polling(const std::map<SensorSource, std::chrono::milliseconds> &intervals) {
        runtime()->assert_external_lifecycle();
        const std::scoped_lock lock(polling_mutex_);
        if (stopped_) {
            throw ClosedError("L30 is stopped");
        }
        auto effective = intervals;
        if (effective.empty()) {
            effective = {{SensorSource::kAngle, kDefaultPollInterval}};
        }
        for (const auto &entry : effective) {
            const auto source = entry.first;
            const auto interval = entry.second;
            if (interval.count() <= 0) {
                throw ValidationError("polling interval for " + std::string(sensor_source_name(source)) +
                                      " must be positive");
            }
        }
        {
            const std::lock_guard<std::mutex> error_lock(polling_wait_mutex_);
            polling_errors_.clear();
        }
        std::vector<runtime::DeviceRuntime::PollTask> tasks;
        for (const auto &entry : effective) {
            const auto source = entry.first;
            const auto interval = entry.second;
            runtime::DeviceRuntime::PollTask task;
            task.name = std::string(sensor_source_name(source));
            task.period = interval;
            task.isolate_failure = true;
            task.run = [this, source](runtime::CancellationToken token) {
                try {
                    send_sense_request(source);
                } catch (const Client::QuarantinedError &) {
                    const std::lock_guard<std::mutex> lock(polling_wait_mutex_);
                    polling_errors_[source] = std::current_exception();
                    throw;
                } catch (...) {
                    if (token.cancelled()) {
                        throw;
                    }
                    const std::lock_guard<std::mutex> lock(polling_wait_mutex_);
                    polling_errors_[source] = std::current_exception();
                }
            };
            tasks.push_back(std::move(task));
        }
        // Runtime replaces the workers after bounded in-flight reads finish.
        // An explicit stop here would cancel them and poison L30 response IDs.
        runtime()->start_polling(std::move(tasks));
    }

    void L30::stop_polling() {
        runtime()->assert_external_lifecycle();
        const std::scoped_lock lock(polling_mutex_);
        stop_polling_locked();
    }

    std::exception_ptr L30::polling_error(SensorSource source) const {
        const std::lock_guard<std::mutex> lock(polling_wait_mutex_);
        const auto it = polling_errors_.find(source);
        return it == polling_errors_.end() ? std::exception_ptr{} : it->second;
    }

    void L30::stop_polling_locked() { runtime()->stop_polling(); }

    void L30::push_event(const SensorEvent &event) {
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_) {
            event_queue_->push(event);
        }
    }

    void L30::send_sense_request(SensorSource source) {
        switch (source) {
        case SensorSource::kAngle:
            angle.send_sense_request();
            break;
        case SensorSource::kSpeed:
            speed.send_sense_request();
            break;
        case SensorSource::kCurrent:
            current.send_sense_request();
            break;
        case SensorSource::kTemperature:
            temperature.send_sense_request();
            break;
        case SensorSource::kFault:
            fault.send_sense_request();
            break;
        case SensorSource::kForceSensor:
            force_sensor.send_sense_request();
            break;
        }
    }

} // namespace linkerbot::hand::l30
