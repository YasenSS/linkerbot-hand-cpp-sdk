#pragma once

#include <linkerbot/hand/health.hpp>
#include <linkerbot/hand/protocol/transaction.hpp>
#include <linkerbot/transport/session.hpp>

#include <any>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace linkerbot::hand::unified_detail {
    class RuntimeAccess;
}

namespace linkerbot::hand::runtime {
    using Clock = transport::Clock;
    using Deadline = transport::Deadline;
    using CancellationToken = transport::CancellationToken;
    using CancellationSource = transport::CancellationSource;
    using CancelledError = transport::CancelledError;
    using TransactionPlan = protocol::TransactionPlan;

    Deadline deadline_after(std::chrono::milliseconds timeout);
    std::chrono::milliseconds remaining(Deadline deadline);

    enum class DeviceState { ready, closing, closed, recovering, faulted };

    struct Sample {
        std::any value;
        bool valid = false;
        Deadline received_at{};
        uint64_t physical_generation = 0;
        uint64_t device_generation = 0;
        uint64_t sequence = 0;
        protocol::Origin origin = protocol::Origin::unknown;
        Deadline started_at{};
        uint64_t transaction_generation = 0;
        uint64_t receive_sequence = 0;
        [[nodiscard]] Clock::duration age(Deadline now = Clock::now()) const { return now - received_at; }
    };

    // Source-compatibility view for callers written before HandHealth. Existing
    // fields and meanings are retained throughout the 0.2.x release line; new code
    // should prefer DeviceRuntime::health(). Binary layout is not guaranteed.
    struct RuntimeHealth {
        DeviceState state = DeviceState::ready;
        uint64_t physical_generation = 0;
        uint64_t device_generation = 1;
        uint64_t completed = 0;
        uint64_t timeouts = 0;
        uint64_t cancelled = 0;
        uint64_t protocol_errors = 0;
        uint64_t late_frames = 0;
        uint64_t event_drops = 0;
        uint64_t duplicate_frames = 0;
        uint64_t conflicting_duplicate_frames = 0;
        uint64_t sent_frames = 0;
        size_t in_flight = 0;
        bool polling_faulted = false;
        std::string polling_error;
        std::string last_error;
    };

    class Request {
    public:
        Request() = default;
        Sample wait_until(Deadline deadline) const;
        Sample wait() const;
        // Wait for backend acceptance only, leaving a response transaction active.
        // This preserves legacy request-only APIs that never waited for a response.
        transport::SendResult wait_sent_until(Deadline deadline) const;
        void cancel() const;
        bool completed() const;
        // A pending claimed receipt may still finish backend I/O after a wait timeout.
        std::optional<transport::SendResult> send_result() const;

    private:
        friend class DeviceRuntime;
        struct State;
        explicit Request(std::shared_ptr<State> state);
        std::shared_ptr<State> state_;
    };

    // Shared execution core for all six model facades. Protocol plans own codecs
    // and assemblers; runtime owns every pending slot, clock, wait and publication.
    class DeviceRuntime : public std::enable_shared_from_this<DeviceRuntime> {
    public:
        using HandlerId = uint64_t;
        using Decoder = std::function<protocol::Progress(const can::Message &)>;
        using DecoderFactory = std::function<Decoder()>;
        using EventHandler = std::function<void(const Sample &)>;
        static std::shared_ptr<DeviceRuntime> acquire(std::shared_ptr<transport::Session> session, std::string identity,
                                                      std::vector<transport::RouteKey> routes,
                                                      size_t inbox_capacity = 512);
        static std::shared_ptr<DeviceRuntime> borrow(can::CANDispatcher &dispatcher, std::string identity,
                                                     std::vector<transport::RouteKey> routes,
                                                     size_t inbox_capacity = 512);
        ~DeviceRuntime();
        DeviceRuntime(const DeviceRuntime &) = delete;
        DeviceRuntime &operator=(const DeviceRuntime &) = delete;

        Request submit(TransactionPlan plan, Deadline deadline, CancellationToken cancellation = {});
        Sample execute(TransactionPlan plan, Deadline deadline, CancellationToken cancellation = {});
        template <class T> T query(TransactionPlan plan, Deadline deadline, CancellationToken cancellation = {}) {
            return std::any_cast<T>(execute(std::move(plan), deadline, cancellation).value);
        }
        void command(TransactionPlan plan, Deadline deadline = Deadline::max(), CancellationToken cancellation = {});
        std::optional<Sample> snapshot(const std::string &domain) const;
        template <class T> std::optional<T> snapshot_as(const std::string &domain) const {
            const auto sample = snapshot(domain);
            if (!sample || !sample->valid) {
                return std::nullopt;
            }
            return std::any_cast<T>(sample->value);
        }

        // Stateless pure decoders for actual unsolicited reports. Stateful report
        // assemblers must use observe_factory so recovery and frame loss reset them.
        // Request assemblers never live here. Duplicate domains are rejected.
        HandlerId observe(std::string domain, Decoder decoder);
        HandlerId observe_factory(std::string domain, DecoderFactory factory);
        void unobserve(HandlerId id);
        // User callbacks execute on a bounded event worker, never the bus RX path.
        HandlerId subscribe(std::string domain, EventHandler handler);
        // false means self-unsubscribe: admission stopped, but this callback still
        // executes. Retaining the id lets a later external caller await its exit.
        bool unsubscribe(HandlerId id);
        void invalidate(const std::string &domain);
        void publish(const std::string &domain, std::any value);
        Sample publish(const std::string &domain, std::any value, Sample provenance);
        // Same-thread provenance from the latest successful execute/query response.
        // A new execute clears it; recovery/close makes old provenance unusable.
        Sample response_provenance() const;

        // A semantic operation can span multiple execute calls using one absolute
        // deadline. It excludes other caller threads on this device only, without
        // holding a physical writer lock across ACK waits. Recursive on its owner.
        class Operation {
        public:
            Operation() = default;
            Operation(Operation &&) noexcept;
            Operation &operator=(Operation &&) noexcept;
            ~Operation();
            // Keep facade call accounting alive until the entire composite returns.
            void retain_call(std::shared_ptr<void> call) { call_ = std::move(call); }
            [[nodiscard]] Deadline deadline() const { return deadline_; }
            Operation(const Operation &) = delete;
            Operation &operator=(const Operation &) = delete;

        private:
            friend class DeviceRuntime;
            explicit Operation(std::shared_ptr<DeviceRuntime> runtime, std::string group, Deadline deadline);
            std::shared_ptr<DeviceRuntime> runtime_;
            std::string group_;
            Deadline deadline_ = Deadline::max();
            std::shared_ptr<void> call_;
        };
        Operation operation(Deadline deadline, CancellationToken cancellation = {});
        // Serialize only callers of this named composite operation. Requests from
        // other domains/devices still use their independent correlation keys.
        Operation operation(std::string group, Deadline deadline, CancellationToken cancellation = {});

        // Adapter for the legacy caller-supplied cross-device transaction mutex.
        // The caller owns the mutex; admission observes the same deadline/cancel contract.
        std::unique_lock<std::timed_mutex> lock_external(std::timed_mutex &gate, Deadline deadline,
                                                         CancellationToken cancellation = {});

        struct PollTask {
            struct Channel {
                std::string name;
                std::chrono::microseconds period{100000};
            };
            std::string name;
            std::chrono::microseconds period{100000};
            std::function<void(CancellationToken)> run;
            // Optional model policy computes its next due time outside runtime locks.
            // nullopt ends this task; runtime owns its wait, wake and cancellation.
            std::function<std::optional<Deadline>()> next;
            // Legacy models stop only the failed source, leaving other tasks running.
            bool isolate_failure = false;
            // A simple task defaults to {name, period}. Composite schedulers declare
            // every logical channel and its independent requested period here.
            std::vector<Channel> channels;
        };
        void start_polling(std::vector<PollTask> tasks);
        void stop_polling();
        // Waits for poll tasks to return on their own without cancelling an
        // in-flight request. Returns false at the deadline and leaves them running.
        [[nodiscard]] bool wait_polling_stopped(Deadline deadline);
        void wake_polling();
        void wait_until(Deadline deadline, CancellationToken cancellation = {}) const;
        uint64_t sent_frames(const std::string &domain) const;
        // Complete public snapshot. This reads only cached runtime/session state and
        // never performs device I/O or waits for a response.
        [[nodiscard]] HandHealth health() const;
        // Runtime-internal metric ingestion used by polling and command adapters.
        // All methods are thread-safe and update only cached diagnostic state.
        void register_channel(std::string channel, double requested_hz = 0.0, bool supported = true);
        void register_command_family(std::string family);
        void record_channel_success(std::string channel, HealthTimePoint received_at = HealthClock::now(),
                                    std::optional<HealthDuration> latency = std::nullopt,
                                    uint64_t feedback_samples = 1);
        void record_channel_timeout(std::string channel);
        void record_deadline_miss(std::string channel, uint64_t count = 1);
        void record_command_accepted(std::string family);
        void record_command_dispatched(std::string family);
        void record_command_acknowledged(std::string family);
        void record_command_failed(std::string family, std::optional<ErrorCode> reason = std::nullopt);
        void record_command_superseded(std::string family);
        void record_error(std::exception_ptr error, ErrorContext context = {},
                          ErrorCode fallback = ErrorCode::kInternal);
        // Compatibility adapter retained throughout 0.2.x while callers migrate
        // to HandHealth. Its counters are runtime totals rather than per-channel
        // data; calling it performs no device I/O.
        [[nodiscard]] RuntimeHealth runtime_health() const;
        size_t pending_count(const std::string &domain) const;
        bool wait_for_fault(Deadline deadline) const;
        std::shared_ptr<transport::Session> session() const;
        void set_routes(std::vector<transport::RouteKey> keys);
        void cancel_all();
        void cancel_domain(const std::string &domain, bool preserve_snapshot = false);
        // Facades call this before taking any lifecycle lock.
        void assert_external_lifecycle() const;
        bool is_event_thread() const;
        // Facade destructor fallback on the event worker: reject admission, stop
        // facade-owned polling, close routes, drain sends, and disable callbacks.
        // Does not take the lifecycle lock or join this event callback. A retained
        // runtime may remain closing until an external close or final destruction.
        void quiesce_for_destruction();
        void close();
        void recover();

    private:
        friend class linkerbot::hand::unified_detail::RuntimeAccess;
        struct Impl;
        explicit DeviceRuntime(std::unique_ptr<Impl> impl);
        void start();
        void close_impl();
        void release_operation(const std::string &group) noexcept;
        std::unique_ptr<Impl> impl_;
    };
} // namespace linkerbot::hand::runtime
