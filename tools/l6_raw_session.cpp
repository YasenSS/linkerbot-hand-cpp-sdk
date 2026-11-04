#include "l6_raw_session.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/l6/frame.hpp>

#ifdef __linux__
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linkerbot/can/socketcan_frame.hpp>
#endif

namespace l6_benchmark {
    using linkerbot::can::Message;
    using linkerbot::hand::l6::SampleOrigin;

#ifdef __linux__
    namespace {
        class FilteredSocketCanTransport final : public linkerbot::can::CANTransport {
        public:
            FilteredSocketCanTransport(std::string_view interface_name, uint32_t arbitration_id) {
                if (interface_name.empty() || interface_name.size() >= IFNAMSIZ) {
                    throw std::invalid_argument("SocketCAN interface name is empty or too long");
                }
                const int opened = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
                fd_.store(opened);
                if (opened < 0) {
                    throw linkerbot::can::TransportIOError("Failed to create benchmark CAN socket",
                                                           std::error_code(errno, std::generic_category()));
                }
                try {
                    struct can_filter filter {};
                    filter.can_id = arbitration_id;
                    filter.can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;
                    if (::setsockopt(opened, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter)) < 0) {
                        throw_system("Failed to set exact CAN_RAW_FILTER");
                    }
                    int receive_own = 0;
                    if (::setsockopt(opened, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &receive_own, sizeof(receive_own)) <
                        0) {
                        throw_system("Failed to disable CAN_RAW_RECV_OWN_MSGS");
                    }

                    struct ifreq request {};
                    std::memcpy(request.ifr_name, interface_name.data(), interface_name.size());
                    request.ifr_name[interface_name.size()] = '\0';
                    if (::ioctl(opened, SIOCGIFINDEX, &request) < 0) {
                        throw_system("Failed to resolve SocketCAN interface");
                    }

                    struct sockaddr_can address {};
                    address.can_family = AF_CAN;
                    address.can_ifindex = request.ifr_ifindex;
                    if (::bind(opened, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) < 0) {
                        throw_system("Failed to bind benchmark CAN socket");
                    }

                    struct timeval timeout {};
                    timeout.tv_usec = 10'000;
                    if (::setsockopt(opened, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                        throw_system("Failed to set benchmark CAN receive timeout");
                    }
                } catch (...) {
                    (void)::close(fd_.exchange(-1));
                    throw;
                }
            }

            ~FilteredSocketCanTransport() override {
                try {
                    close();
                    // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
                    // NOLINTNEXTLINE(bugprone-empty-catch)
                } catch (...) {
                }
            }

            std::optional<Message> read() override {
                const int fd = require_open();
                struct can_frame frame {};
                const auto bytes = ::read(fd, &frame, sizeof(frame));
                if (bytes == static_cast<ssize_t>(sizeof(frame))) {
                    return linkerbot::can::decode_socketcan_frame(frame);
                }
                if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    return std::nullopt;
                }
                if (bytes < 0) {
                    throw_system("Benchmark CAN read failed");
                }
                throw linkerbot::can::TransportIOError("Benchmark CAN read returned an unexpected frame size");
            }

            void write(const Message &message) override {
                const auto encoded =
                    linkerbot::can::encode_socketcan_frame(linkerbot::can::LinkKind::kClassic, message);
                const auto bytes = ::write(require_open(), encoded.data(), encoded.size());
                if (bytes != static_cast<ssize_t>(encoded.size())) {
                    throw_system("Benchmark CAN write failed");
                }
            }

            void close() override {
                const int fd = fd_.exchange(-1);
                if (fd >= 0) {
                    (void)::close(fd);
                }
            }

        private:
            [[noreturn]] static void throw_system(const char *message) {
                throw linkerbot::can::TransportIOError(message, std::error_code(errno, std::generic_category()));
            }

            [[nodiscard]] int require_open() const {
                const int fd = fd_.load();
                if (fd < 0) {
                    throw linkerbot::can::ClosedError("Benchmark SocketCAN transport is closed");
                }
                return fd;
            }

            std::atomic<int> fd_{-1};
        };
    } // namespace
#endif

    std::unique_ptr<linkerbot::can::CANTransport> make_socketcan_transport(std::string_view interface_name,
                                                                           uint32_t arbitration_id) {
#ifdef __linux__
        return std::make_unique<FilteredSocketCanTransport>(interface_name, arbitration_id);
#else
        (void)interface_name;
        (void)arbitration_id;
        throw std::runtime_error("SocketCAN benchmark requires Linux");
#endif
    }

    RawSession::RawSession(std::unique_ptr<linkerbot::can::CANTransport> transport, uint32_t arbitration_id,
                           std::chrono::milliseconds late_response_guard)
        : arbitration_id_(arbitration_id),
          dispatcher_(std::make_unique<linkerbot::can::CANDispatcher>(std::move(transport))),
          late_response_guard_(late_response_guard) {
        subscription_id_ = dispatcher_->subscribe([this](const Message &message) { on_message(message); });
    }

    RawSession::~RawSession() {
        if (dispatcher_) {
            dispatcher_->unsubscribe(subscription_id_);
            dispatcher_->stop();
        }
    }

    void RawSession::wait_for_quarantine(Clock::time_point deadline) {
        Clock::time_point quarantine;
        {
            const std::scoped_lock lock(state_mutex_);
            quarantine = quarantine_until_;
        }
        if (Clock::now() < quarantine) {
            std::this_thread::sleep_until(std::min(quarantine, deadline));
        }
        if (Clock::now() >= deadline) {
            throw std::runtime_error("Raw L6 transaction timed out in late-response guard");
        }
    }

    RawAngleSample RawSession::transact_angle(const Message &request, SampleOrigin origin,
                                              std::chrono::milliseconds timeout) {
        const auto deadline = Clock::now() + timeout;
        std::unique_lock transaction_lock(transaction_mutex_);
        check_stopped();
        wait_for_quarantine(deadline);
        {
            const std::scoped_lock lock(state_mutex_);
            angle_pending_ = AnglePending{origin, Clock::now(), {}, {}};
        }
        try {
            dispatcher_->send(request);
            {
                const std::scoped_lock lock(metrics_mutex_);
                ++metrics_.sent_count;
                ++metrics_.angle_sent_count;
            }
        } catch (...) {
            const std::scoped_lock lock(state_mutex_);
            angle_pending_.reset();
            throw;
        }

        std::unique_lock state_lock(state_mutex_);
        const auto ready = [this] {
            return angle_pending_ && (angle_pending_->sample || !angle_pending_->error.empty());
        };
        while (!ready() && Clock::now() < deadline && !(stop_ && stop_())) {
            state_cv_.wait_until(state_lock, std::min(deadline, Clock::now() + std::chrono::milliseconds{5}));
        }
        const bool notified = ready();
        if (!notified) {
            angle_pending_.reset();
            quarantine_until_ = Clock::now() + late_response_guard_;
            state_lock.unlock();
            const std::scoped_lock metrics_lock(metrics_mutex_);
            ++metrics_.timeout_count;
            ++metrics_.angle_timeout_count;
            throw std::runtime_error("Raw L6 angle transaction timed out");
        }
        if (!angle_pending_->error.empty()) {
            const auto error = angle_pending_->error;
            angle_pending_.reset();
            quarantine_until_ = Clock::now() + late_response_guard_;
            throw std::runtime_error(error);
        }
        const auto &pending_sample = angle_pending_->sample;
        if (!pending_sample.has_value()) {
            throw std::runtime_error("Raw L6 angle transaction completed without a sample");
        }
        auto sample = *pending_sample;
        angle_pending_.reset();
        return sample;
    }

    RawAngleSample RawSession::query_angle(std::chrono::milliseconds timeout) {
        Message request;
        request.id = arbitration_id_;
        request.dlc = 1;
        request.data[0] = linkerbot::hand::l6::kAngleOpcode;
        return transact_angle(request, SampleOrigin::Query, timeout);
    }

    std::optional<RawAngleSample> RawSession::command_angle(const std::array<uint8_t, 6> &target, bool expect_feedback,
                                                            std::chrono::milliseconds timeout) {
        Message request;
        request.id = arbitration_id_;
        request.dlc = 7;
        request.data[0] = linkerbot::hand::l6::kAngleOpcode;
        std::copy(target.begin(), target.end(), request.data.begin() + 1);
        if (expect_feedback) {
            return transact_angle(request, SampleOrigin::CommandFeedback, timeout);
        }

        const auto deadline = Clock::now() + timeout;
        std::unique_lock transaction_lock(transaction_mutex_);
        check_stopped();
        wait_for_quarantine(deadline);
        dispatcher_->send(request);
        const std::scoped_lock metrics_lock(metrics_mutex_);
        ++metrics_.sent_count;
        ++metrics_.angle_sent_count;
        return std::nullopt;
    }

    RawSession::FingerTiming RawSession::query_finger(uint8_t opcode, Clock::time_point deadline) {
        std::unique_lock transaction_lock(transaction_mutex_);
        check_stopped();
        wait_for_quarantine(deadline);
        {
            const std::scoped_lock lock(state_mutex_);
            tactile_pending_ = TactilePending{opcode, Clock::now(), {}, 0, {}};
        }

        Message request;
        request.id = arbitration_id_;
        request.dlc = 2;
        request.data[0] = opcode;
        request.data[1] = linkerbot::hand::l6::kForceSelectByte;
        try {
            dispatcher_->send(request);
        } catch (...) {
            const std::scoped_lock lock(state_mutex_);
            tactile_pending_.reset();
            quarantine_until_ = Clock::now() + late_response_guard_;
            throw;
        }
        {
            const std::scoped_lock lock(metrics_mutex_);
            ++metrics_.sent_count;
            ++metrics_.tactile_sent_count;
        }

        std::unique_lock state_lock(state_mutex_);
        const auto ready = [this] {
            return tactile_pending_ && (tactile_pending_->count == 12 || !tactile_pending_->error.empty());
        };
        while (!ready() && Clock::now() < deadline && !(stop_ && stop_())) {
            state_cv_.wait_until(state_lock, std::min(deadline, Clock::now() + std::chrono::milliseconds{5}));
        }
        const bool notified = ready();
        if (!notified) {
            tactile_pending_.reset();
            quarantine_until_ = Clock::now() + late_response_guard_;
            state_lock.unlock();
            const std::scoped_lock metrics_lock(metrics_mutex_);
            ++metrics_.timeout_count;
            ++metrics_.tactile_timeout_count;
            if (opcode >= 0xB1 && opcode <= 0xB5) {
                ++metrics_.tactile_finger_timeout_count[opcode - 0xB1];
            }
            throw std::runtime_error("Raw L6 tactile finger timed out");
        }
        if (!tactile_pending_->error.empty()) {
            const auto error = tactile_pending_->error;
            tactile_pending_.reset();
            quarantine_until_ = Clock::now() + late_response_guard_;
            throw std::runtime_error(error);
        }
        const auto completed = tactile_pending_->completed_at;
        const auto latency =
            std::chrono::duration_cast<std::chrono::microseconds>(completed - tactile_pending_->sent_at);
        tactile_pending_.reset();
        return FingerTiming{latency, completed};
    }

    std::chrono::microseconds RawSession::query_all_tactile(std::chrono::milliseconds timeout,
                                                            const BeforeFinger &before_finger) {
        static constexpr std::array<uint8_t, 5> kFingerOpcodes{
            linkerbot::hand::l6::kThumbForceOpcode, linkerbot::hand::l6::kIndexForceOpcode,
            linkerbot::hand::l6::kMiddleForceOpcode, linkerbot::hand::l6::kRingForceOpcode,
            linkerbot::hand::l6::kPinkyForceOpcode};
        const std::scoped_lock batch_lock(tactile_batch_mutex_);
        struct ActiveBatch {
            std::atomic<bool> &active;
            explicit ActiveBatch(std::atomic<bool> &value) : active(value) { active.store(true); }
            ~ActiveBatch() { active.store(false); }
        } active(tactile_batch_active_);
        const auto started_at = Clock::now();
        const auto deadline = started_at + timeout;
        Clock::time_point completed_at{};
        for (size_t i = 0; i < kFingerOpcodes.size(); ++i) {
            check_stopped();
            if (before_finger) {
                before_finger(i, deadline);
            }
            check_stopped();
            const auto duration = query_finger(kFingerOpcodes[i], deadline);
            const std::scoped_lock lock(metrics_mutex_);
            finger_latencies_us_[i].push_back(static_cast<double>(duration.latency.count()));
            completed_at = duration.timestamp;
        }
        const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started_at);
        {
            const std::scoped_lock lock(metrics_mutex_);
            ++metrics_.completed_count;
            ++metrics_.tactile_completed_count;
            window_.force(completed_at);
            tactile_latencies_us_.push_back(static_cast<double>(latency.count()));
        }
        return latency;
    }

    void RawSession::set_reply_enabled(bool enabled) {
        Message request;
        request.id = arbitration_id_;
        request.dlc = 7;
        request.data.fill(linkerbot::hand::l6::kReplyOpcode);
        request.data[6] = enabled ? uint8_t{0x01} : linkerbot::hand::l6::kReplyOpcode;
        std::unique_lock transaction_lock(transaction_mutex_);
        dispatcher_->send(request);
    }

    void RawSession::on_message(const Message &message) {
        if (message.id != arbitration_id_ || message.dlc == 0) {
            return;
        }
        if (message.data[0] == linkerbot::hand::l6::kAngleOpcode) {
            on_angle_message(message);
            return;
        }
        if (message.data[0] >= linkerbot::hand::l6::kThumbForceOpcode &&
            message.data[0] <= linkerbot::hand::l6::kPinkyForceOpcode) {
            on_tactile_message(message);
        }
    }

    void RawSession::on_angle_message(const Message &message) {
        std::unique_lock state_lock(state_mutex_);
        if (!linkerbot::hand::l6::is_classic_response(message, arbitration_id_, 7, linkerbot::hand::l6::kAngleOpcode)) {
            if (angle_pending_) {
                angle_pending_->error = "Invalid raw L6 angle response frame";
                state_cv_.notify_all();
            }
            state_lock.unlock();
            const std::scoped_lock metrics_lock(metrics_mutex_);
            ++metrics_.protocol_error_count;
            return;
        }
        if (Clock::now() < quarantine_until_ || !angle_pending_) {
            state_lock.unlock();
            const std::scoped_lock metrics_lock(metrics_mutex_);
            ++metrics_.late_response_count;
            return;
        }
        if (angle_pending_->sample) {
            state_lock.unlock();
            const std::scoped_lock lock(metrics_mutex_);
            ++metrics_.duplicate_count;
            return;
        }
        RawAngleSample sample;
        std::copy_n(message.data.begin() + 1, 6, sample.raw.begin());
        sample.origin = angle_pending_->origin;
        sample.timestamp = Clock::now();
        sample.latency =
            std::chrono::duration_cast<std::chrono::microseconds>(sample.timestamp - angle_pending_->sent_at);
        angle_pending_->sample = sample;
        const std::scoped_lock metrics_lock(metrics_mutex_);
        window_.angle(sample.timestamp, sample.origin);
        ++metrics_.completed_count;
        ++metrics_.angle_feedback_count;
        if (sample.origin == SampleOrigin::Query) {
            ++metrics_.angle_query_count;
        } else if (sample.origin == SampleOrigin::CommandFeedback) {
            ++metrics_.angle_command_feedback_count;
        } else {
            ++metrics_.unknown_origin_count;
        }
        angle_latencies_us_.push_back(static_cast<double>(sample.latency.count()));
        state_cv_.notify_all();
    }

    void RawSession::on_tactile_message(const Message &message) {
        std::unique_lock state_lock(state_mutex_);
        if (!tactile_pending_ || Clock::now() < quarantine_until_) {
            state_lock.unlock();
            const std::scoped_lock metrics_lock(metrics_mutex_);
            ++metrics_.late_response_count;
            return;
        }
        if (!linkerbot::hand::l6::is_classic_can(message) || message.dlc != 8 ||
            message.data[0] != tactile_pending_->opcode) {
            tactile_pending_->error = "Invalid raw L6 tactile response frame";
            state_cv_.notify_all();
            state_lock.unlock();
            const std::scoped_lock metrics_lock(metrics_mutex_);
            ++metrics_.protocol_error_count;
            return;
        }

        uint8_t row = message.data[1];
        if (row >= 12) {
            if ((row & 0x0F) != 0) {
                tactile_pending_->error = "Invalid raw L6 tactile row marker";
                state_cv_.notify_all();
                state_lock.unlock();
                const std::scoped_lock metrics_lock(metrics_mutex_);
                ++metrics_.protocol_error_count;
                return;
            }
            row = static_cast<uint8_t>(row >> 4);
        }
        if (row >= 12) {
            tactile_pending_->error = "Raw L6 tactile row is out of range";
            state_cv_.notify_all();
            state_lock.unlock();
            const std::scoped_lock metrics_lock(metrics_mutex_);
            ++metrics_.protocol_error_count;
            return;
        }

        std::array<uint8_t, 6> payload{};
        std::copy_n(message.data.begin() + 2, 6, payload.begin());
        auto &slot = tactile_pending_->rows[row];
        if (slot) {
            const bool conflict = *slot != payload;
            if (conflict) {
                tactile_pending_->error = "Raw L6 tactile duplicate row conflicts";
            }
            state_cv_.notify_all();
            state_lock.unlock();
            const std::scoped_lock metrics_lock(metrics_mutex_);
            if (conflict) {
                ++metrics_.conflicting_duplicate_count;
                ++metrics_.protocol_error_count;
            } else {
                ++metrics_.duplicate_count;
            }
            return;
        }
        slot = payload;
        ++tactile_pending_->count;
        if (tactile_pending_->count == 12) {
            tactile_pending_->completed_at = Clock::now();
            state_cv_.notify_all();
        }
    }

    void RawSession::check_stopped() const {
        if (stop_ && stop_()) {
            throw std::runtime_error("Raw L6 session stopped");
        }
    }

    void RawSession::set_measurement_window(Clock::time_point begin, Clock::time_point end) {
        const std::scoped_lock lock(metrics_mutex_);
        window_.begin = begin;
        window_.end = end;
    }

    void RawSession::reset_metrics() {
        const std::scoped_lock lock(metrics_mutex_);
        metrics_ = Metrics{};
        window_ = WindowSamples{};
        for (auto &values : finger_latencies_us_) {
            values.clear();
        }
        angle_latencies_us_.clear();
        tactile_latencies_us_.clear();
    }

    Metrics RawSession::metrics() const {
        const std::scoped_lock lock(state_mutex_, metrics_mutex_);
        auto result = metrics_;
        result.pending_count =
            static_cast<uint64_t>(angle_pending_.has_value()) + static_cast<uint64_t>(tactile_pending_.has_value());
        result.in_flight_count = static_cast<uint64_t>(angle_pending_.has_value()) +
                                 static_cast<uint64_t>(tactile_batch_active_.load() || tactile_pending_.has_value());
        window_.apply(result);
        for (size_t i = 0; i < 5; ++i) {
            result.finger_latency[i] = summarize(finger_latencies_us_[i]);
        }
        result.angle_latency = summarize(angle_latencies_us_);
        result.tactile_latency = summarize(tactile_latencies_us_);
        return result;
    }

    linkerbot::can::TransportHealth RawSession::health() const { return dispatcher_->get_health(); }
} // namespace l6_benchmark
