#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l6/angle.hpp>
#include <linkerbot/hand/l6/current.hpp>
#include <linkerbot/hand/l6/current_limit.hpp>
#include <linkerbot/hand/l6/detail/interstage.hpp>
#include <linkerbot/hand/l6/events.hpp>
#include <linkerbot/hand/l6/force_sensor.hpp>
#include <linkerbot/hand/l6/frame.hpp>
#include <linkerbot/hand/l6/l6.hpp>
#include <linkerbot/hand/l6/runtime.hpp>
#include <linkerbot/hand/l6/temperature.hpp>
#include <linkerbot/hand/l6/torque.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef __linux__
#include <linkerbot/can/socketcan_transport.hpp>
#endif

namespace linkerbot::hand::l6 {

    namespace {
        constexpr uint32_t side_to_id(Side side) {
            return side == Side::kLeft ? kLeftArbitrationId : kRightArbitrationId;
        }

        std::unique_ptr<can::CANTransport> make_transport(std::string_view interface_name) {
#ifdef __linux__
            return std::make_unique<can::SocketCANTransport>(interface_name);
#else
            (void)interface_name;
            throw TransportError("CAN transport is only supported on Linux. "
                                 "Use L6 with a custom CANTransport on other platforms.");
#endif
        }

        bool contains_source(const std::vector<SensorSource> &sources, SensorSource source) {
            return std::find(sources.begin(), sources.end(), source) != sources.end();
        }

        void require_positive_period(bool enabled, std::chrono::microseconds period, const char *name) {
            if (enabled && period.count() <= 0) {
                throw ValidationError(std::string(name) + " period must be positive");
            }
        }
    } // namespace

    L6::L6(Side side, std::string_view interface_name, const std::optional<std::filesystem::path> &angle_mapping_path)
        : L6(side, make_transport(interface_name), interface_name, angle_mapping_path) {}

    L6::L6(Side side, std::unique_ptr<can::CANTransport> transport, std::string_view interface_name,
           const std::optional<std::filesystem::path> &angle_mapping_path)
        : L6(side, transport::Session::own(std::move(transport)), interface_name, angle_mapping_path) {
        owns_session_ = true;
    }

    L6::L6(Side side, std::shared_ptr<transport::Session> session, std::string_view interface_name,
           const std::optional<std::filesystem::path> &angle_mapping_path)
        : arbitration_id_(side_to_id(side)), session_(std::move(session)),
          runtime_(runtime::DeviceRuntime::acquire(session_, "l6/" + std::to_string(arbitration_id_),
                                                   {{arbitration_id_, false}})),
          angle(arbitration_id_, runtime_, angle_mapping_path, side == Side::kLeft ? "left" : "right", interface_name),
          force_sensor(arbitration_id_, runtime_), current_limit(arbitration_id_, runtime_),
          torque(arbitration_id_, runtime_), speed(arbitration_id_, runtime_), temperature(arbitration_id_, runtime_),
          current(arbitration_id_, runtime_), fault(arbitration_id_, runtime_), version(arbitration_id_, runtime_),
          stall(arbitration_id_, runtime_), config(arbitration_id_, runtime_),
          position_kp(arbitration_id_, runtime_, kPositionKpOpcode, "position_kp", 200),
          position_kd(arbitration_id_, runtime_, kPositionKdOpcode, "position_kd"),
          torque_kd(arbitration_id_, runtime_, kTorqueKdOpcode, "torque_kd"),
          limit_compensation(arbitration_id_, runtime_, kLimitCompensationOpcode, "limit_compensations") {
        for (const auto *channel : {"angles", "currents", "force", "temperatures", "faults", "version", "stall"}) {
            runtime_->register_channel(channel);
        }
        runtime_->register_channel("speeds", 0.0, false);
        for (const auto *channel : {"position_kp", "position_kd", "torque_kd", "limit_compensations"}) {
            runtime_->register_channel(channel);
        }
        for (const auto *family : {"angle_targets", "speed_targets", "current_limits", "torque_targets", "fault_reset",
                                   "stall", "configuration"}) {
            runtime_->register_command_family(family);
        }
        start();
    }

    L6::~L6() {
        try {
            stop();
        } catch (...) // NOLINT(bugprone-empty-catch) destructor must not throw
        {
            try {
                stop_stream();
                runtime_->quiesce_for_destruction();
                // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
                // NOLINTNEXTLINE(bugprone-empty-catch)
            } catch (...) {
            }
            // Runtime cancellation wakes requests but does not account for the
            // facade code that follows them. Drain before stats_/scheduler state
            // (declared after the managers) is destroyed, including event-thread
            // destruction where public stop() is deliberately rejected.
            try {
                drain_manager_calls();
                // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
                // NOLINTNEXTLINE(bugprone-empty-catch)
            } catch (...) {
            }
        }
    }

    const DeviceCapabilities &L6::capabilities() const noexcept {
        auto side = HandSide::kUnknown;
        if (arbitration_id_ == side_to_id(Side::kLeft)) {
            side = HandSide::kLeft;
        } else if (arbitration_id_ == side_to_id(Side::kRight)) {
            side = HandSide::kRight;
        }
        return declared_capabilities(Model::kL6, side);
    }

    void L6::drain_manager_calls() {
        angle.channel_.drain_calls();
        force_sensor.channel_.drain_calls();
        for (auto &[name, finger] : force_sensor.fingers_) {
            (void)name;
            finger->channel_.drain_calls();
        }
        current_limit.channel_.drain_calls();
        torque.channel_.drain_calls();
        speed.channel_.drain_calls();
        temperature.channel_.drain_calls();
        current.channel_.drain_calls();
        fault.channel_.drain_calls();
        version.serial_.drain_calls();
        version.pcb_.drain_calls();
        version.firmware_.drain_calls();
        version.mechanical_.drain_calls();
        stall.time_.drain_calls();
        stall.threshold_.drain_calls();
        stall.torque_.drain_calls();
        config.ids_.drain_calls();
        position_kp.channel_.drain_calls();
        position_kd.channel_.drain_calls();
        torque_kd.channel_.drain_calls();
        limit_compensation.channel_.drain_calls();
    }

    void L6::start() {
        angle.start();
        force_sensor.start();
        current_limit.start();
        torque.start();
        temperature.start();
        current.start();
        fault.start();
        version.start();
        stall.start();
        config.start();
        speed.start();
        position_kp.start();
        position_kd.start();
        torque_kd.start();
        limit_compensation.start();

        angle.attach_stats(&stats_);
        force_sensor.attach_stats(&stats_);
        config.set_reply_change_listener([this](bool enabled) { angle.set_reply_enabled(enabled); });
        angle.set_command_feedback_listener([this]() { on_command_feedback(); });

        angle.set_event_sink([this](const AngleData &d) { push_event(AngleEvent{d}); });
        current_limit.set_event_sink([this](const CurrentLimitData &d) { push_event(CurrentLimitEvent{d}); });
        torque.set_event_sink([this](const TorqueData &d) { push_event(TorqueEvent{d}); });
        temperature.set_event_sink([this](const TemperatureData &d) { push_event(TemperatureEvent{d}); });
        current.set_event_sink([this](const CurrentData &d) { push_event(CurrentEvent{d}); });
        fault.set_event_sink([this](const FaultData &d) { push_event(FaultEvent{d}); });
        force_sensor.set_event_sink([this](const AllFingersData &d) { push_event(ForceSensorEvent{d}); });
    }

    EventQueue<SensorEvent> &L6::stream(size_t maxsize) { return *stream_handle(maxsize); }

    std::shared_ptr<EventQueue<SensorEvent>> L6::stream_handle(size_t maxsize) {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(stop_mutex_);
        if (runtime_->runtime_health().state != runtime::DeviceState::ready) {
            throw LifecycleError("L6 runtime is not ready for streaming");
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

    void L6::stop_stream() {
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_) {
            event_queue_->close();
        }
    }

    void L6::stop() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> stop_lock(stop_mutex_);
        // Every stage below must run even if an earlier one throws, so a
        // failure in one manager's stop() (or the dispatcher's) cannot leave
        // the others' threads/subscriptions unstopped. The first exception
        // seen is rethrown once every stage has been attempted.
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

        run_stage([this] { runtime_->cancel_all(); });
        run_stage([this] { angle.cancel_waiters(); });
        run_stage([this] { force_sensor.cancel_waiters(); });
        run_stage([this] { current_limit.cancel_waiters(); });
        run_stage([this] { torque.cancel_waiters(); });
        run_stage([this] { temperature.cancel_waiters(); });
        run_stage([this] { current.cancel_waiters(); });
        run_stage([this] { fault.cancel_waiters(); });
        run_stage([this] { speed.channel_.cancel(); });
        run_stage([this] {
            version.serial_.cancel();
            version.pcb_.cancel();
            version.firmware_.cancel();
            version.mechanical_.cancel();
        });
        run_stage([this] {
            stall.time_.cancel();
            stall.threshold_.cancel();
            stall.torque_.cancel();
        });
        run_stage([this] { config.ids_.cancel(); });
        run_stage([this] { position_kp.channel_.cancel(); });
        run_stage([this] { position_kd.channel_.cancel(); });
        run_stage([this] { torque_kd.channel_.cancel(); });
        run_stage([this] { limit_compensation.channel_.cancel(); });
        run_stage([this] { stop_stream(); });
        run_stage([this] { stop_polling(); });

        run_stage([this] { angle.stop(); });
        run_stage([this] { force_sensor.stop(); });
        run_stage([this] { current_limit.stop(); });
        run_stage([this] { torque.stop(); });
        run_stage([this] { speed.channel_.stop(); });
        run_stage([this] { temperature.stop(); });
        run_stage([this] { current.stop(); });
        run_stage([this] { fault.stop(); });
        run_stage([this] { version.stop(); });
        run_stage([this] { stall.stop(); });
        run_stage([this] { config.stop(); });
        run_stage([this] { position_kp.stop(); });
        run_stage([this] { position_kd.stop(); });
        run_stage([this] { torque_kd.stop(); });
        run_stage([this] { limit_compensation.stop(); });

        run_stage([this] { runtime_->close(); });
        if (owns_session_) {
            run_stage([this] { session_->close(); });
        }

        if (first_error) {
            std::rethrow_exception(first_error);
        }
    }

    void L6::recover() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> stop_lock(stop_mutex_);
        stop_polling();
        stop_stream();
        runtime_->recover();
        // Reply configuration is unknown after physical recovery.
        angle.set_reply_enabled(std::nullopt);
        {
            const std::lock_guard<std::mutex> lock(config.reply_mutex_);
            config.reply_enabled_.reset();
        }
    }

    Snapshot L6::get_snapshot() const {
        return Snapshot{
            angle.get_snapshot(),        current_limit.get_snapshot(),     torque.get_snapshot(),
            temperature.get_snapshot(),  current.get_snapshot(),           fault.get_snapshot(),
            force_sensor.get_snapshot(), stall.get_time_snapshot(),        stall.get_threshold_snapshot(),
            stall.get_torque_snapshot(), std::chrono::steady_clock::now(), position_kp.get_snapshot(),
            position_kd.get_snapshot(),  torque_kd.get_snapshot(),         limit_compensation.get_snapshot(),
            speed.get_snapshot(),
        };
    }

    L6PollingPlan L6::make_plan_from_sources(const std::vector<SensorSource> &sources,
                                             std::chrono::milliseconds interval_ms) const {
        L6PollingPlan plan;
        const auto period = std::chrono::duration_cast<std::chrono::microseconds>(interval_ms);
        plan.angle_period = period;
        plan.tactile_period = period;
        plan.current_limit_period = period;
        plan.torque_period = period;
        plan.temperature_period = period;
        plan.current_period = period;
        plan.fault_period = period;
        const bool all = sources.empty();
        plan.angle_enabled = all || contains_source(sources, SensorSource::kAngle);
        plan.tactile_enabled = all || contains_source(sources, SensorSource::kForceSensor);
        plan.current_limit_enabled = all || contains_source(sources, SensorSource::kCurrentLimit);
        plan.torque_enabled = all || contains_source(sources, SensorSource::kTorque);
        plan.temperature_enabled = all || contains_source(sources, SensorSource::kTemperature);
        plan.current_enabled = all || contains_source(sources, SensorSource::kCurrent);
        plan.fault_enabled = all || contains_source(sources, SensorSource::kFault);
        plan.angle_policy = AngleFeedbackPolicy::QueryOnly;
        plan.request_timeout = std::max(interval_ms, std::chrono::milliseconds{100});
        plan.tactile_timeout = std::max(interval_ms, std::chrono::milliseconds{1000});
        return plan;
    }

    void L6::start_polling(const std::vector<SensorSource> &sources, std::chrono::milliseconds interval_ms) {
        start_polling(make_plan_from_sources(sources, interval_ms));
    }

    void L6::start_polling(const L6PollingPlan &plan) {
        runtime_->assert_external_lifecycle();
        detail::validate_interstage(plan.interstage);
        require_positive_period(plan.angle_enabled, plan.angle_period, "angle");
        require_positive_period(plan.tactile_enabled, plan.tactile_period, "tactile");
        require_positive_period(plan.current_limit_enabled, plan.current_limit_period, "current_limit");
        require_positive_period(plan.torque_enabled, plan.torque_period, "torque");
        require_positive_period(plan.temperature_enabled, plan.temperature_period, "temperature");
        require_positive_period(plan.current_enabled, plan.current_period, "current");
        require_positive_period(plan.fault_enabled, plan.fault_period, "fault");
        if (plan.request_timeout.count() <= 0 || plan.tactile_timeout.count() <= 0) {
            throw ValidationError("request timeouts must be positive");
        }
        if (plan.late_response_guard.count() < 0) {
            throw ValidationError("late_response_guard must be non-negative");
        }
        if (plan.angle_enabled && plan.angle_policy == AngleFeedbackPolicy::CommandFeedbackOnly) {
            const auto reply = config.reply_enabled();
            if (!reply.has_value() || !*reply) {
                throw DeviceNotReadyError("CommandFeedbackOnly requires config.set_reply_enabled(true); "
                                          "L6 0xCC angle replies are not known to be enabled");
            }
        }

        const std::scoped_lock lock(polling_mutex_);
        stop_polling_locked();
        {
            const std::scoped_lock scheduler_lock(scheduler_mutex_);
            apply_plan_locked(plan);
        }
        polling_active_ = true;
        try {
            runtime::DeviceRuntime::PollTask task{
                "l6.schedule", std::chrono::microseconds{1}, [this](runtime::CancellationToken) { scheduler_step(); },
                [this]() {
                    const std::lock_guard<std::mutex> lock(scheduler_mutex_);
                    if (!polling_allowed()) {
                        return std::optional<runtime::Deadline>(std::nullopt);
                    }
                    const auto next = next_deadline_locked();
                    if (!next) {
                        return std::optional<runtime::Deadline>(std::nullopt);
                    }
                    return std::optional<runtime::Deadline>(plan_.polling_until ? std::min(*next, *plan_.polling_until)
                                                                                : *next);
                }};
            const auto add_channel = [&task](const char *name, bool enabled, auto period) {
                if (enabled) {
                    task.channels.push_back({name, period});
                }
            };
            add_channel("angles", plan.angle_enabled, plan.angle_period);
            add_channel("force", plan.tactile_enabled, plan.tactile_period);
            add_channel("currents", plan.current_limit_enabled, plan.current_limit_period);
            add_channel("torque", plan.torque_enabled, plan.torque_period);
            add_channel("temperatures", plan.temperature_enabled, plan.temperature_period);
            add_channel("currents", plan.current_enabled, plan.current_period);
            add_channel("faults", plan.fault_enabled, plan.fault_period);
            runtime_->start_polling({std::move(task)});
        } catch (...) {
            polling_active_ = false;
            throw;
        }
    }

    void L6::apply_plan_locked(const L6PollingPlan &plan) {
        plan_ = plan;
        const auto now = std::chrono::steady_clock::now();
        auto init = [now](PollChannel &channel, bool enabled, std::chrono::microseconds period) {
            channel.enabled = enabled;
            channel.period = period;
            channel.next = now;
        };
        init(angle_channel_, plan.angle_enabled, plan.angle_period);
        init(tactile_channel_, plan.tactile_enabled, plan.tactile_period);
        init(current_limit_channel_, plan.current_limit_enabled, plan.current_limit_period);
        init(torque_channel_, plan.torque_enabled, plan.torque_period);
        init(temperature_channel_, plan.temperature_enabled, plan.temperature_period);
        init(current_channel_, plan.current_enabled, plan.current_period);
        init(fault_channel_, plan.fault_enabled, plan.fault_period);
        if (plan.angle_enabled && plan.angle_policy == AngleFeedbackPolicy::CommandFeedbackOnly) {
            angle_channel_.next = now + plan.angle_period;
        }
        angle.set_late_response_guard(plan.late_response_guard);
        angle.set_command_timeout(plan.request_timeout);
        force_sensor.set_late_response_guard(plan.late_response_guard);
    }

    void L6::stop_polling() {
        runtime_->assert_external_lifecycle();
        const std::scoped_lock lock(polling_mutex_);
        stop_polling_locked();
    }

    bool L6::stop_polling_gracefully(std::chrono::milliseconds timeout) {
        runtime_->assert_external_lifecycle();
        if (timeout.count() < 0) {
            throw ValidationError("polling drain timeout must be non-negative");
        }
        const std::scoped_lock lock(polling_mutex_);
        polling_active_ = false;
        runtime_->wake_polling();
        const auto drained = runtime_->wait_polling_stopped(std::chrono::steady_clock::now() + timeout);
        if (!drained) {
            runtime_->stop_polling();
        }
        return drained;
    }

    void L6::stop_polling_locked() {
        polling_active_ = false;
        runtime_->stop_polling();
    }

    RuntimeStatsSnapshot L6::get_runtime_stats() const {
        auto snap = stats_.snapshot();
        {
            const std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            const auto core = runtime_->runtime_health();
            snap.protocol_error_count = core.protocol_errors - stats_baseline_.protocol_errors;
            snap.late_response_count = core.late_frames - stats_baseline_.late_frames;
            snap.duplicate_count = core.duplicate_frames - stats_baseline_.duplicate_frames;
            snap.conflicting_duplicate_count =
                core.conflicting_duplicate_frames - stats_baseline_.conflicting_duplicate_frames;
            snap.sent_count = core.sent_frames - stats_baseline_.sent_frames;
            snap.angle_sent_count = runtime_->sent_frames("angle") - angle_sent_baseline_;
            uint64_t tactile = 0;
            for (unsigned op = 0xb1; op <= 0xb5; ++op) {
                tactile += runtime_->sent_frames("tactile/" + std::to_string(op));
            }
            snap.tactile_sent_count = tactile - tactile_sent_baseline_;
            snap.pending_count = snap.in_flight_count = core.in_flight;
            snap.event_drop_count = core.event_drops - stats_baseline_.event_drops;
        }
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_) {
            snap.event_drop_count += event_queue_->drop_count();
        }
        return snap;
    }

    can::TransportHealth L6::get_transport_health() const { return session_->transport_health(); }

    void L6::reset_runtime_stats() {
        stats_.reset();
        {
            const std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_baseline_ = runtime_->runtime_health();
            angle_sent_baseline_ = runtime_->sent_frames("angle");
            tactile_sent_baseline_ = 0;
            for (unsigned op = 0xb1; op <= 0xb5; ++op) {
                tactile_sent_baseline_ += runtime_->sent_frames("tactile/" + std::to_string(op));
            }
        }
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_) {
            event_queue_->reset_drop_count();
        }
    }

    void L6::push_event(const SensorEvent &event) {
        const std::scoped_lock lock(event_mutex_);
        if (event_queue_) {
            event_queue_->push(event);
        }
    }

    void L6::on_command_feedback() { runtime_->wake_polling(); }

    std::optional<std::chrono::steady_clock::time_point> L6::next_deadline_locked() const {
        std::optional<std::chrono::steady_clock::time_point> next;
        auto consider = [&next](const PollChannel &channel) {
            if (!channel.enabled) {
                return;
            }
            if (!next || channel.next < *next) {
                next = channel.next;
            }
        };
        consider(angle_channel_);
        consider(current_limit_channel_);
        consider(torque_channel_);
        consider(temperature_channel_);
        consider(current_channel_);
        consider(fault_channel_);
        consider(tactile_channel_);
        return next;
    }

    bool L6::take_due_locked(PollChannel &channel, std::chrono::steady_clock::time_point now) {
        if (!channel.enabled || channel.next > now) {
            return false;
        }
        const auto elapsed = now - channel.next;
        stats_.record_deadline_jitter(std::chrono::duration_cast<std::chrono::microseconds>(elapsed));
        const auto periods = static_cast<uint64_t>(elapsed / channel.period) + 1;
        channel.next += periods * channel.period;
        if (periods > 1) {
            stats_.add_skipped_deadlines(periods - 1);
            const auto channel_name = [&]() {
                if (&channel == &angle_channel_) {
                    return "angles";
                }
                if (&channel == &tactile_channel_) {
                    return "force";
                }
                if (&channel == &temperature_channel_) {
                    return "temperatures";
                }
                if (&channel == &fault_channel_) {
                    return "faults";
                }
                if (&channel == &torque_channel_) {
                    return "torque";
                }
                return "currents";
            }();
            runtime_->record_deadline_miss(channel_name, periods - 1);
            if (&channel == &angle_channel_) {
                stats_.add_angle_skipped_deadline_count(periods - 1);
            }
            if (&channel == &tactile_channel_) {
                stats_.add_tactile_skipped_deadline_count(periods - 1);
            }
        }
        stats_.inc_scheduled();
        return true;
    }

    bool L6::polling_allowed() const {
        return polling_active_.load() &&
               (!plan_.polling_until || std::chrono::steady_clock::now() < *plan_.polling_until);
    }

    bool L6::take_angle_due_locked(std::chrono::steady_clock::time_point now,
                                   std::chrono::steady_clock::time_point &due) {
        due = angle_channel_.next;
        if (!take_due_locked(angle_channel_, now)) {
            return false;
        }
        stats_.inc_angle_scheduled();
        if (plan_.angle_policy == AngleFeedbackPolicy::CommandFeedbackOnly) {
            return false;
        }
        if (plan_.angle_policy == AngleFeedbackPolicy::Adaptive) {
            const auto last = angle.last_command_feedback_time();
            return detail::adaptive_requires_query(last, due);
        }
        return true;
    }

    void L6::query_scheduled_angle(std::chrono::steady_clock::time_point due, runtime::Deadline deadline) {
        struct ScheduledQuery {
            std::chrono::steady_clock::time_point due;
            runtime::Deadline deadline;
        };
        const auto timing = ScheduledQuery{due, deadline};
        const auto sample = angle.get_until(timing.deadline);
        stats_.record_angle_deadline_latency(
            std::chrono::duration_cast<std::chrono::microseconds>(sample.timestamp - timing.due));
    }

    void L6::before_tactile_finger(size_t index, std::chrono::steady_clock::time_point deadline) {
        if (!plan_.angle_enabled || plan_.angle_policy == AngleFeedbackPolicy::CommandFeedbackOnly) {
            return;
        }
        auto options = plan_.interstage;
        // P2 only optimizes query-only polling. Control arbitration is separate.
        options.query_lookahead = options.query_lookahead && plan_.angle_policy == AngleFeedbackPolicy::QueryOnly;
        for (unsigned dispatched = 0; dispatched < options.max_angle_transactions;) {
            if (!polling_allowed()) {
                throw LifecycleError("L6 polling stopped at finger boundary");
            }
            std::chrono::steady_clock::time_point due;
            detail::BoundaryAction action;
            {
                const std::scoped_lock lock(scheduler_mutex_);
                due = angle_channel_.next;
                action = detail::boundary_action(options, std::chrono::steady_clock::now(), due, angle_channel_.period,
                                                 deadline, 5 - index, dispatched);
            }
            if (action == detail::BoundaryAction::ContinueTactile) {
                return;
            }
            if (action == detail::BoundaryAction::BudgetExhausted) {
                stats_.add_interstage_budget_rejection_count();
                return;
            }
            if (action == detail::BoundaryAction::WaitForAngle) {
                const auto started = std::chrono::steady_clock::now();
                runtime_->wait_until(due);
                stats_.add_interstage_wait_count();
                stats_.record_interstage_wait(
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started));
                continue;
            }
            const auto started = std::chrono::steady_clock::now();
            const auto request_deadline = std::min(deadline, started + options.request_timeout);
            if (request_deadline <= started) {
                stats_.add_interstage_budget_rejection_count();
                return;
            }
            {
                const std::scoped_lock lock(scheduler_mutex_);
                if (!take_angle_due_locked(started, due)) {
                    return;
                }
            }
            ++dispatched;
            stats_.add_interstage_angle_count();
            // Failure aborts the outer batch rather than swallowing an error
            // and continuing its next finger after a failed nested transaction.
            try {
                query_scheduled_angle(due, request_deadline);
                stats_.record_interstage_work(
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started));
            } catch (const internal::DataRelayTimeout &) {
                stats_.inc_interstage_angle_timeout();
                stats_.record_interstage_work(
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started));
                throw;
            }
        }
    }

    void L6::scheduler_step() {
        if (polling_allowed()) {
            bool run_angle = false;
            std::chrono::steady_clock::time_point angle_due;
            bool run_current_limit = false;
            bool run_torque = false;
            bool run_temperature = false;
            bool run_current = false;
            bool run_fault = false;
            bool run_tactile = false;
            {
                std::unique_lock lock(scheduler_mutex_);
                const auto now = std::chrono::steady_clock::now();
                run_angle = take_angle_due_locked(now, angle_due);
                run_current_limit = take_due_locked(current_limit_channel_, now);
                run_torque = take_due_locked(torque_channel_, now);
                run_temperature = take_due_locked(temperature_channel_, now);
                run_current = take_due_locked(current_channel_, now);
                run_fault = take_due_locked(fault_channel_, now);
                run_tactile = take_due_locked(tactile_channel_, now);
                if (run_tactile) {
                    stats_.inc_tactile_scheduled();
                }
            }

            if (!polling_allowed()) {
                return;
            }
            if (run_angle) {
                try {
                    query_scheduled_angle(angle_due, runtime::deadline_after(plan_.request_timeout));
                    // The loop rechecks cancellation and the shared deadline after this bounded wait.
                    // NOLINTNEXTLINE(bugprone-empty-catch)
                } catch (const internal::DataRelayTimeout &) {
                }
            }
            if (run_current_limit && polling_allowed()) {
                try {
                    (void)current_limit.get_blocking(plan_.request_timeout);
                    // The loop rechecks cancellation and the shared deadline after this bounded wait.
                    // NOLINTNEXTLINE(bugprone-empty-catch)
                } catch (const internal::DataRelayTimeout &) {
                }
            }
            if (run_torque && polling_allowed()) {
                const auto deadline = std::chrono::steady_clock::now() + plan_.request_timeout;
                try {
                    (void)torque.get_blocking(plan_.request_timeout);
                    stats_.inc_completed();
                } catch (const internal::DataRelayTimeout &) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        stats_.inc_timeout();
                    }
                }
            }
            if (run_temperature && polling_allowed()) {
                const auto deadline = std::chrono::steady_clock::now() + plan_.request_timeout;
                try {
                    (void)temperature.get_blocking(plan_.request_timeout);
                    stats_.inc_completed();
                } catch (const internal::DataRelayTimeout &) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        stats_.inc_timeout();
                    }
                }
            }
            if (run_current && polling_allowed()) {
                const auto deadline = std::chrono::steady_clock::now() + plan_.request_timeout;
                try {
                    (void)current.get_blocking(plan_.request_timeout);
                    stats_.inc_completed();
                } catch (const internal::DataRelayTimeout &) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        stats_.inc_timeout();
                    }
                }
            }
            if (run_fault && polling_allowed()) {
                const auto deadline = std::chrono::steady_clock::now() + plan_.request_timeout;
                try {
                    (void)fault.get_blocking(plan_.request_timeout);
                    stats_.inc_completed();
                } catch (const internal::DataRelayTimeout &) {
                    if (std::chrono::steady_clock::now() >= deadline) {
                        stats_.inc_timeout();
                    }
                }
            }
            if (run_tactile && polling_allowed()) {
                try {
                    const auto started = std::chrono::steady_clock::now();
                    const auto deadline = started + plan_.tactile_timeout;
                    auto service_deadline = std::min(deadline, started + plan_.tactile_period);
                    if (plan_.polling_until) {
                        service_deadline = std::min(service_deadline, *plan_.polling_until);
                    }
                    (void)force_sensor.get_staged(deadline, [this, service_deadline](size_t index, auto hard_deadline) {
                        if (!polling_allowed() || !session_->health().open) {
                            throw LifecycleError("L6 polling stopped or disconnected");
                        }
                        before_tactile_finger(index, std::min(service_deadline, hard_deadline));
                    });
                    if (std::chrono::steady_clock::now() > started + plan_.tactile_period) {
                        stats_.add_tactile_service_overrun_count();
                    }
                    // The loop rechecks cancellation and the shared deadline after this bounded wait.
                    // NOLINTNEXTLINE(bugprone-empty-catch)
                } catch (const internal::DataRelayTimeout &) {
                } catch (const LifecycleError &) {
                    if (polling_allowed()) {
                        throw;
                    }
                }
            }
        }
    }

} // namespace linkerbot::hand::l6
