#include <chrono>
#include <cstddef>
#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/o30/events.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef __linux__
#include <linkerbot/can/socketcan_transport.hpp>
#endif

namespace linkerbot::hand::o30 {

    namespace {
        std::unique_ptr<can::CANTransport> make_transport(std::string_view interface_name) {
#ifdef __linux__
            return std::make_unique<can::SocketCANTransport>(interface_name, true);
#else
            (void)interface_name;
            throw TransportError("SocketCAN FD is only supported on Linux. "
                                 "Construct O30 with a custom CANTransport on other platforms.");
#endif
        }
    } // namespace

    std::string_view sensor_source_name(SensorSource source) {
        switch (source) {
        case SensorSource::kAngle:
            return "angle";
        case SensorSource::kSpeed:
            return "speed";
        case SensorSource::kAcceleration:
            return "acceleration";
        case SensorSource::kCurrent:
            return "current";
        case SensorSource::kCurrentLimit:
            return "current_limit";
        case SensorSource::kVoltage:
            return "voltage";
        case SensorSource::kTorque:
            return "torque";
        case SensorSource::kTemperature:
            return "temperature";
        case SensorSource::kStallTime:
            return "stall_time";
        case SensorSource::kFault:
            return "fault";
        case SensorSource::kForceSensor:
        case SensorSource::kAllForceSensors:
            return "force";
        }
        throw ValidationError("unknown O30 sensor source");
    }

    O30::O30(std::string_view interface_name, uint32_t request_id) : O30(make_transport(interface_name), request_id) {}

    O30::O30(std::unique_ptr<can::CANTransport> transport, uint32_t request_id)
        : O30(transport::Session::own(std::move(transport)), request_id) {
        owns_session_ = true;
    }

    O30::O30(can::CANDispatcher &dispatcher, uint32_t request_id)
        : O30(transport::Session::borrow(dispatcher), request_id) {}

    O30::O30(std::shared_ptr<transport::Session> session, uint32_t request_id)
        : session_(std::move(session)),
          client_(std::make_unique<Client>(
              runtime::DeviceRuntime::acquire(session_, "o30/" + std::to_string(request_id),
                                              {{protocol::expected_response_id(request_id), false}}),
              request_id)),
          mapping(*client_), angle(*client_), speed(*client_), acceleration(*client_), current(*client_),
          voltage(*client_), torque(*client_), temperature(*client_), stall(*client_), enable(*client_),
          fault(*client_), pid(*client_), sensor(*client_), config(*client_), preset(*client_), version(*client_),
          service(*client_), unit(*client_), comm_error(*client_) {
        for (const auto *channel :
             {"angles", "speeds", "currents", "force", "temperatures", "faults", "version", "stall"}) {
            client_->runtime()->register_channel(channel);
        }
        for (const auto *family : {"angle_targets", "speed_targets", "current_limits", "acceleration_targets", "stall",
                                   "configuration", "hop_writes"}) {
            client_->runtime()->register_command_family(family);
        }
        start();
    }

    O30::~O30() {
        try {
            stop();
        } catch (...) // NOLINT(bugprone-empty-catch) destructor must not throw
        {
            try {
                stop_stream();
                client_->runtime()->quiesce_for_destruction();
            } catch (...) {
            }
        }
    }

    uint32_t O30::request_id() const { return client_->request_id(); }

    uint32_t O30::response_id() const { return client_->response_id(); }

    const DeviceCapabilities &O30::capabilities() const noexcept {
        return declared_capabilities(Model::kO30, HandSide::kUnknown);
    }

    void O30::start() {
        angle.set_event_sink([this](const AngleData &d) { push_event(AngleEvent{d}); });
        speed.set_event_sink([this](const SpeedData &d) { push_event(SpeedEvent{d}); });
        acceleration.set_event_sink([this](const AccelerationData &d) { push_event(AccelerationEvent{d}); });
        current.set_event_sink([this](const CurrentData &d) { push_event(CurrentEvent{d}); });
        current.set_limit_event_sink([this](const CurrentLimitData &d) { push_event(CurrentLimitEvent{d}); });
        voltage.set_event_sink([this](const VoltageData &d) { push_event(VoltageEvent{d}); });
        torque.set_event_sink([this](const TorqueData &d) { push_event(TorqueEvent{d}); });
        temperature.set_event_sink([this](const TemperatureData &d) { push_event(TemperatureEvent{d}); });
        stall.set_event_sink([this](const StallTimeData &d) { push_event(StallTimeEvent{d}); });
        fault.set_event_sink([this](const FaultData &d) { push_event(FaultEvent{d}); });
        sensor.set_event_sink([this](const SensorData &d) { push_event(ForceSensorEvent{d}); });
    }

    void O30::stop() {
        client_->runtime()->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lock(stop_mutex_);
        std::exception_ptr error;
        try {
            stop_stream();
        } catch (...) {
            error = std::current_exception();
        }
        try {
            client_->close();
        } catch (...) {
            if (!error) {
                error = std::current_exception();
            }
        }
        try {
            if (owns_session_) {
                session_->close();
            }
        } catch (...) {
            if (!error) {
                error = std::current_exception();
            }
        }
        if (error) {
            std::rethrow_exception(error);
        }
    }

    Snapshot O30::get_snapshot() const {
        return Snapshot{
            angle.get_snapshot(),
            speed.get_snapshot(),
            acceleration.get_snapshot(),
            current.get_snapshot(),
            current.get_current_limit_snapshot(),
            voltage.get_snapshot(),
            torque.get_snapshot(),
            temperature.get_snapshot(),
            stall.get_times_snapshot(),
            stall.get_thresholds_snapshot(),
            stall.get_hold_currents_snapshot(),
            fault.get_snapshot(),
            sensor.get_snapshot(),
            std::chrono::steady_clock::now(),
        };
    }

    EventQueue<SensorEvent> &O30::stream(size_t maxsize) { return *stream_handle(maxsize); }

    std::shared_ptr<EventQueue<SensorEvent>> O30::stream_handle(size_t maxsize) {
        client_->runtime()->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(stop_mutex_);
        if (client_->runtime()->runtime_health().state != runtime::DeviceState::ready) {
            throw LifecycleError("O30 runtime is not ready for streaming");
        }
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_ && !event_queue_->closed()) {
            throw LifecycleError("stream() already active; call stop_stream() first");
        }
        if (event_queue_) {
            retired_streams_.push_back(event_queue_);
        }
        event_queue_ = std::make_shared<EventQueue<SensorEvent>>(maxsize);
        return event_queue_;
    }

    void O30::stop_stream() {
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_) {
            event_queue_->close();
        }
    }

    void O30::recover() {
        client_->runtime()->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(stop_mutex_);
        stop_stream();
        client_->runtime()->recover();
    }

    void O30::start_polling(const std::map<SensorSource, std::chrono::milliseconds> &intervals) {
        auto effective = intervals;
        if (effective.empty()) {
            effective = {{SensorSource::kAngle, kDefaultPollInterval}};
        }
        std::vector<runtime::DeviceRuntime::PollTask> tasks;
        for (const auto &entry : effective) {
            const auto source = entry.first;
            const auto interval = entry.second;
            if (interval.count() <= 0) {
                throw PollingPlanError(std::string(sensor_source_name(source)) + " poll interval must be positive");
            }
            runtime::DeviceRuntime::PollTask task;
            task.name = std::string(sensor_source_name(source));
            task.period = interval;
            task.run = [this, source](runtime::CancellationToken) { send_sense_request(source); };
            task.isolate_failure = true;
            tasks.push_back(std::move(task));
        }
        client_->runtime()->start_polling(std::move(tasks));
    }

    void O30::stop_polling() { client_->runtime()->stop_polling(); }

    void O30::push_event(const SensorEvent &event) {
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_) {
            event_queue_->push(event);
        }
    }

    void O30::send_sense_request(SensorSource source) {
        switch (source) {
        case SensorSource::kAngle:
            angle.send_sense_request();
            break;
        case SensorSource::kSpeed:
            speed.send_sense_request();
            break;
        case SensorSource::kAcceleration:
            acceleration.send_sense_request();
            break;
        case SensorSource::kCurrent:
            current.send_sense_request();
            break;
        case SensorSource::kCurrentLimit:
            static_cast<void>(current.get_current_limits());
            break;
        case SensorSource::kVoltage:
            voltage.send_sense_request();
            break;
        case SensorSource::kTorque:
            torque.send_sense_request();
            break;
        case SensorSource::kTemperature:
            temperature.send_sense_request();
            break;
        case SensorSource::kStallTime:
            stall.send_sense_request();
            break;
        case SensorSource::kFault:
            fault.send_sense_request();
            break;
        case SensorSource::kForceSensor:
            static_cast<void>(sensor.read_data());
            break;
        case SensorSource::kAllForceSensors:
            static_cast<void>(sensor.read_all_data());
            break;
        }
    }

} // namespace linkerbot::hand::o30
