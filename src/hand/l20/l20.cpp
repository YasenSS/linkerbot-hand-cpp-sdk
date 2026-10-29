#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20/angle.hpp>
#include <linkerbot/hand/l20/current.hpp>
#include <linkerbot/hand/l20/events.hpp>
#include <linkerbot/hand/l20/force_sensor.hpp>
#include <linkerbot/hand/l20/l20.hpp>
#include <linkerbot/hand/l20/speed.hpp>
#include <linkerbot/hand/l20/temperature.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef __linux__
#include <linkerbot/can/socketcan_transport.hpp>
#endif

namespace linkerbot::hand::l20 {

    namespace {
        constexpr uint32_t side_to_id(Side side) { return side == Side::kLeft ? 0x28 : 0x27; }

        std::unique_ptr<can::CANTransport> make_transport(std::string_view interface_name) {
#ifdef __linux__
            return std::make_unique<can::SocketCANTransport>(interface_name);
#else
            (void)interface_name;
            throw TransportError("CAN transport is only supported on Linux. "
                                 "Use L20 with a custom CANTransport on other platforms.");
#endif
        }

        constexpr std::string_view sensor_source_name(SensorSource source) {
            switch (source) {
            case SensorSource::kAngle:
                return "angle";
            case SensorSource::kCurrent:
                return "current";
            case SensorSource::kSpeed:
                return "speed";
            case SensorSource::kTemperature:
                return "temperature";
            case SensorSource::kFault:
                return "fault";
            case SensorSource::kForceSensor:
                return "force_sensor";
            }
            return "unknown";
        }

    } // namespace

    L20::L20(Side side, std::string_view interface_name, const std::optional<std::filesystem::path> &angle_mapping_path)
        : L20(side, make_transport(interface_name), interface_name, angle_mapping_path) {}

    L20::L20(Side side, std::unique_ptr<can::CANTransport> transport, std::string_view interface_name,
             const std::optional<std::filesystem::path> &angle_mapping_path)
        : L20(side, transport::Session::own(std::move(transport)), interface_name, angle_mapping_path) {
        owns_session_ = true;
    }

    L20::L20(Side side, std::shared_ptr<transport::Session> session, std::string_view interface_name,
             const std::optional<std::filesystem::path> &angle_mapping_path)
        : arbitration_id_(side_to_id(side)), session_(std::move(session)),
          runtime_(runtime::DeviceRuntime::acquire(session_, "l20/" + std::to_string(arbitration_id_),
                                                   {{arbitration_id_, false}})),
          angle(arbitration_id_, runtime_, angle_mapping_path, side == Side::kLeft ? "left" : "right", interface_name),
          force_sensor(arbitration_id_, runtime_), current(arbitration_id_, runtime_),
          current_limit(arbitration_id_, runtime_), speed(arbitration_id_, runtime_),
          temperature(arbitration_id_, runtime_), fault(arbitration_id_, runtime_), version(arbitration_id_, runtime_) {
        for (const auto *channel : {"angles", "speeds", "currents", "force", "temperatures", "faults", "version"}) {
            runtime_->register_channel(channel);
        }
        runtime_->register_channel("stall", 0.0, false);
        for (const auto *family : {"angle_targets", "speed_targets", "current_limits", "fault_reset"}) {
            runtime_->register_command_family(family);
        }
        start();
    }

    L20::~L20() {
        try {
            stop();
        } catch (...) // NOLINT(bugprone-empty-catch) destructor must not throw
        {
            try {
                stop_stream();
                runtime_->quiesce_for_destruction();
            } catch (...) {
            }
        }
    }

    const DeviceCapabilities &L20::capabilities() const noexcept {
        auto side = HandSide::kUnknown;
        if (arbitration_id_ == side_to_id(Side::kLeft)) {
            side = HandSide::kLeft;
        } else if (arbitration_id_ == side_to_id(Side::kRight)) {
            side = HandSide::kRight;
        }
        return declared_capabilities(Model::kL20, side);
    }

    void L20::start() {
        angle.start();
        force_sensor.start();
        current.start();
        current_limit.start();
        speed.start();
        temperature.start();
        fault.start();
        version.start();

        angle.set_event_sink([this](const AngleData &d) { push_event(AngleEvent{d}); });
        current.set_event_sink([this](const CurrentData &d) { push_event(CurrentEvent{d}); });
        current_limit.set_event_sink([this](const CurrentLimitData &d) { push_event(CurrentLimitEvent{d}); });
        speed.set_event_sink([this](const SpeedData &d) { push_event(SpeedEvent{d}); });
        temperature.set_event_sink([this](const TemperatureData &d) { push_event(TemperatureEvent{d}); });
        fault.set_event_sink([this](const FaultData &d) { push_event(FaultEvent{d}); });
        force_sensor.set_event_sink([this](const AllFingersData &d) { push_event(ForceSensorEvent{d}); });
    }

    std::shared_ptr<EventQueue<SensorEvent>> L20::stream_handle(size_t maxsize) {
        const std::scoped_lock lock(event_mutex_);
        if (streaming_closed_) {
            throw LifecycleError("stream() called after stop()");
        }
        if (event_queue_ && !event_queue_->closed()) {
            throw LifecycleError("stream() already active; call stop_stream() first");
        }
        if (event_queue_) {
            retired_streams_.push_back(event_queue_);
        }
        event_queue_ = std::make_shared<EventQueue<SensorEvent>>(maxsize);
        return event_queue_;
    }

    EventQueue<SensorEvent> &L20::stream(size_t maxsize) { return *stream_handle(maxsize); }

    void L20::stop_stream() {
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_) {
            event_queue_->close();
        }
    }

    void L20::stop() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lock(stop_mutex_);

        // Every stage below must run even if an earlier one throws, so a
        // failure in one manager's stop() (or the runtime's) cannot leave
        // the others' subscriptions unstopped. The first exception seen is
        // rethrown once every stage has been attempted.
        std::exception_ptr first_error;
        auto run_stage = [&first_error](auto &&stage) {
            try {
                stage();
            } catch (...) {
                if (!first_error) {
                    first_error = std::current_exception();
                }
            }
        };

        run_stage([this] {
            const std::scoped_lock lock(event_mutex_);
            streaming_closed_ = true;
            if (event_queue_) {
                event_queue_->close();
            }
        });
        run_stage([this] { runtime_->cancel_all(); });
        run_stage([this] { angle.cancel_pending(); });
        run_stage([this] { force_sensor.cancel_pending(); });
        run_stage([this] { current.cancel_pending(); });
        run_stage([this] { current_limit.cancel_pending(); });
        run_stage([this] { speed.cancel_pending(); });
        run_stage([this] { temperature.cancel_pending(); });
        run_stage([this] { fault.cancel_pending(); });
        run_stage([this] { version.cancel_pending(); });
        run_stage([this] { stop_polling(); });

        run_stage([this] { angle.stop(); });
        run_stage([this] { force_sensor.stop(); });
        run_stage([this] { current.stop(); });
        run_stage([this] { current_limit.stop(); });
        run_stage([this] { speed.stop(); });
        run_stage([this] { temperature.stop(); });
        run_stage([this] { fault.stop(); });
        run_stage([this] { version.stop(); });

        run_stage([this] { runtime_->close(); });
        if (owns_session_) {
            run_stage([this] { session_->close(); });
        }

        if (first_error) {
            std::rethrow_exception(first_error);
        }
    }

    // assert_external_lifecycle() runs before any facade lock is taken, per
    // the runtime's lock-order contract. The old stream is retired (not left
    // dangling on a mix of pre/post-recovery samples) while existing shared_ptr
    // holders stay valid until this L20 object is destructed; runtime_->recover()
    // itself invalidates all cached samples, so no per-domain invalidation here.
    void L20::recover() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(stop_mutex_);
        {
            const std::scoped_lock lock(event_mutex_);
            if (event_queue_) {
                event_queue_->close();
                retired_streams_.push_back(event_queue_);
                event_queue_.reset();
            }
        }
        runtime_->recover();
    }

    Snapshot L20::get_snapshot() const {
        return Snapshot{
            angle.get_snapshot(),        current.get_snapshot(),           current_limit.get_snapshot(),
            speed.get_snapshot(),        temperature.get_snapshot(),       fault.get_snapshot(),
            force_sensor.get_snapshot(), std::chrono::steady_clock::now(),
        };
    }

    void L20::start_polling(const std::vector<SensorSource> &sources, std::chrono::milliseconds interval_ms) {
        if (interval_ms.count() <= 0) {
            throw ValidationError("poll interval must be positive");
        }
        auto effective_sources = sources;
        if (effective_sources.empty()) {
            effective_sources = {SensorSource::kAngle,       SensorSource::kCurrent, SensorSource::kSpeed,
                                 SensorSource::kTemperature, SensorSource::kFault,   SensorSource::kForceSensor};
        }

        std::map<SensorSource, std::chrono::milliseconds> intervals;
        for (const auto source : effective_sources) {
            intervals[source] = interval_ms;
        }
        start_polling(intervals);
    }

    void L20::start_polling(const std::map<SensorSource, std::chrono::milliseconds> &intervals) {
        if (intervals.empty()) {
            throw PollingPlanError("polling plan must not be empty");
        }

        std::vector<runtime::DeviceRuntime::PollTask> tasks;
        tasks.reserve(intervals.size());
        for (const auto &entry : intervals) {
            const auto source = entry.first;
            const auto interval = entry.second;
            if (interval.count() <= 0) {
                throw PollingPlanError("poll interval must be positive");
            }
            if (sensor_source_name(source) == "unknown") {
                throw ValidationError("unknown polling source");
            }
            tasks.push_back({std::string(sensor_source_name(source)),
                             interval,
                             [this, source](runtime::CancellationToken) { send_sense_request(source); },
                             {},
                             true});
        }
        runtime_->start_polling(std::move(tasks));
    }

    void L20::stop_polling() { runtime_->stop_polling(); }

    void L20::push_event(const SensorEvent &event) {
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_) {
            event_queue_->push(event);
        }
    }

    void L20::send_sense_request(SensorSource source) {
        switch (source) {
        case SensorSource::kAngle:
            angle.send_sense_request();
            break;
        case SensorSource::kCurrent:
            current.send_sense_request();
            break;
        case SensorSource::kSpeed:
            speed.send_sense_request();
            break;
        case SensorSource::kTemperature:
            temperature.send_sense_request();
            break;
        case SensorSource::kFault:
            fault.send_fault_request();
            break;
        case SensorSource::kForceSensor:
            force_sensor.send_sense_request();
            break;
        }
    }

    bool L20::polling_faulted() const { return runtime_->runtime_health().polling_faulted; }

    std::optional<std::string> L20::last_polling_error() const {
        auto error = runtime_->runtime_health().polling_error;
        if (error.empty()) {
            return std::nullopt;
        }
        return error;
    }

    bool L20::wait_for_polling_fault(std::chrono::milliseconds timeout) const {
        return runtime_->wait_for_fault(runtime::deadline_after(timeout));
    }

} // namespace linkerbot::hand::l20
