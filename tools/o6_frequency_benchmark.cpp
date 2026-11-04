#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/socketcan_frame.hpp>
#include <linkerbot/hand/o6/o6.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <cerrno>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
    using Clock = std::chrono::steady_clock;
    using Message = linkerbot::can::Message;
    using O6 = linkerbot::hand::o6::O6;
    using Side = linkerbot::hand::o6::Side;
    constexpr uint32_t kLeftId = 0x28;
    constexpr uint32_t kRightId = 0x27;
    std::atomic<bool> g_stop{false};

    void signal_handler(int) { g_stop.store(true); }

    enum class Layer { SocketCan, Sdk, Fake };
    enum class DataKind { Angle, Tactile, Combined };
    enum class Mode { Polling, Control };
    enum class AnglePolicy { QueryOnly, CommandFeedbackOnly, Adaptive };
    struct Options {
        Layer layer = Layer::Sdk;
        DataKind data = DataKind::Angle;
        std::string side = "left";
        std::string interface_name = "can0";
        double angle_hz = 50.0;
        double tactile_hz = 10.0;
        double duration_s = 15.0;
        double warmup_s = 2.0;
        int rounds = 1;
        int timeout_ms = 100;
        std::string output;
        std::string firmware = "unknown";
        std::string sensor = "unknown";
        std::string serial = "MASKED";
        std::string adapter = "unknown";
        bool help = false;
        bool dry_run = false;
        Mode mode = Mode::Polling;
        AnglePolicy angle_policy = AnglePolicy::QueryOnly;
        double command_hz = 50.0;
        bool confirm_motion = false;
        double motion_amplitude = 0.0;
    };

    struct Distribution {
        size_t count = 0;
        double min_us = 0, mean_us = 0, p50_us = 0, p95_us = 0, p99_us = 0, max_us = 0;
    };
    Distribution summarize(std::vector<double> values) {
        Distribution out;
        if (values.empty()) {
            return out;
        }
        std::sort(values.begin(), values.end());
        auto pct = [&](double p) { return values[static_cast<size_t>(p * static_cast<double>(values.size() - 1))]; };
        out.count = values.size();
        out.min_us = values.front();
        out.max_us = values.back();
        out.mean_us = 0;
        for (double value : values) {
            out.mean_us += value;
        }
        out.mean_us /= static_cast<double>(values.size());
        out.p50_us = pct(.50);
        out.p95_us = pct(.95);
        out.p99_us = pct(.99);
        return out;
    }

    struct Counters {
        std::atomic<uint64_t> angle_scheduled{0}, angle_sent{0}, angle_completed{0}, angle_skipped{0};
        std::atomic<uint64_t> tactile_scheduled{0}, tactile_sent{0}, tactile_completed{0}, tactile_skipped{0};
        std::atomic<uint64_t> timeout{0}, errors{0}, duplicates{0}, protocol_errors{0};
        std::atomic<uint64_t> query_angle{0}, command_feedback_angle{0}, command_sent{0};
        std::mutex value_mutex;
        std::vector<double> angle_latency, tactile_latency, angle_intervals;
        Clock::time_point last_angle{};
    };

    struct Result {
        int round = 1;
        double requested_duration = 0, actual_duration = 0;
        double requested_angle = 0, requested_tactile = 0;
        double requested_command = 0;
        double actual_angle = 0, actual_tactile = 0, actual_command = 0;
        bool passed = false;
        std::string failure;
        std::shared_ptr<Counters> counters;
        Distribution angle_latency, tactile_latency, angle_interval;
        uint64_t pending = 0, in_flight = 0;
        bool transport_healthy = false;
    };

    void usage() {
        std::cout << "O6 angle/tactile frequency benchmark\n\n"
                  << "Usage: o6_frequency_benchmark --layer socketcan|sdk|fake [options]\n"
                  << "  --interface NAME       SocketCAN interface (default can0)\n"
                  << "  --side left|right      O6 CAN ID (left=0x28, right=0x27)\n"
                  << "  --data angle|tactile|combined\n"
                  << "  --mode polling|control --angle-policy query|command-feedback|adaptive\n"
                  << "  --command-hz HZ --confirm-motion --motion-amplitude VALUE\n"
                  << "  --angle-hz HZ --tactile-hz HZ\n"
                  << "  --duration SEC --warmup SEC --rounds N --timeout-ms N\n"
                  << "  --output FILE          JSON report\n"
                  << "  --firmware-version TEXT --sensor-version TEXT\n"
                  << "  --device-serial MASKED --can-adapter TEXT\n"
                  << "  --dry-run --help\n";
    }

    std::string next_value(int argc, char **argv, int &i, const char *name) {
        if (++i >= argc) {
            throw std::invalid_argument(std::string("missing value after ") + name);
        }
        return argv[i];
    }
    double positive(const std::string &text, const char *name) {
        size_t used = 0;
        double value = std::stod(text, &used);
        if (used != text.size() || !std::isfinite(value) || value <= 0) {
            throw std::invalid_argument(std::string(name) + " must be positive");
        }
        return value;
    }
    int positive_int(const std::string &text, const char *name) {
        size_t used = 0;
        int value = std::stoi(text, &used);
        if (used != text.size() || value <= 0) {
            throw std::invalid_argument(std::string(name) + " must be positive");
        }
        return value;
    }
    Options parse_options(int argc, char **argv) {
        Options o;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--help" || a == "-h") {
                o.help = true;
            } else if (a == "--layer") {
                auto v = next_value(argc, argv, i, "--layer");
                if (v == "socketcan") {
                    o.layer = Layer::SocketCan;
                } else if (v == "sdk") {
                    o.layer = Layer::Sdk;
                } else if (v == "fake") {
                    o.layer = Layer::Fake;
                } else {
                    throw std::invalid_argument("--layer must be socketcan, sdk, or fake");
                }
            } else if (a == "--interface") {
                o.interface_name = next_value(argc, argv, i, "--interface");
            } else if (a == "--side") {
                o.side = next_value(argc, argv, i, "--side");
            } else if (a == "--data") {
                auto v = next_value(argc, argv, i, "--data");
                if (v == "angle") {
                    o.data = DataKind::Angle;
                } else if (v == "tactile") {
                    o.data = DataKind::Tactile;
                } else if (v == "combined") {
                    o.data = DataKind::Combined;
                } else {
                    throw std::invalid_argument("--data must be angle, tactile, or combined");
                }
            } else if (a == "--mode") {
                auto v = next_value(argc, argv, i, "--mode");
                if (v == "polling") {
                    o.mode = Mode::Polling;
                } else if (v == "control") {
                    o.mode = Mode::Control;
                } else {
                    throw std::invalid_argument("--mode must be polling or control");
                }
            } else if (a == "--angle-policy") {
                auto v = next_value(argc, argv, i, "--angle-policy");
                if (v == "query") {
                    o.angle_policy = AnglePolicy::QueryOnly;
                } else if (v == "command-feedback") {
                    o.angle_policy = AnglePolicy::CommandFeedbackOnly;
                } else if (v == "adaptive") {
                    o.angle_policy = AnglePolicy::Adaptive;
                } else {
                    throw std::invalid_argument("--angle-policy must be query, command-feedback, or adaptive");
                }
            } else if (a == "--command-hz") {
                o.command_hz = positive(next_value(argc, argv, i, "--command-hz"), "--command-hz");
            } else if (a == "--confirm-motion") {
                o.confirm_motion = true;
            } else if (a == "--motion-amplitude") {
                o.motion_amplitude = std::stod(next_value(argc, argv, i, "--motion-amplitude"));
            } else if (a == "--angle-hz") {
                o.angle_hz = positive(next_value(argc, argv, i, "--angle-hz"), "--angle-hz");
            } else if (a == "--tactile-hz") {
                o.tactile_hz = positive(next_value(argc, argv, i, "--tactile-hz"), "--tactile-hz");
            } else if (a == "--duration") {
                o.duration_s = positive(next_value(argc, argv, i, "--duration"), "--duration");
            } else if (a == "--warmup") {
                o.warmup_s = std::stod(next_value(argc, argv, i, "--warmup"));
            } else if (a == "--rounds") {
                o.rounds = positive_int(next_value(argc, argv, i, "--rounds"), "--rounds");
            } else if (a == "--timeout-ms") {
                o.timeout_ms = positive_int(next_value(argc, argv, i, "--timeout-ms"), "--timeout-ms");
            } else if (a == "--output") {
                o.output = next_value(argc, argv, i, "--output");
            } else if (a == "--firmware-version") {
                o.firmware = next_value(argc, argv, i, "--firmware-version");
            } else if (a == "--sensor-version") {
                o.sensor = next_value(argc, argv, i, "--sensor-version");
            } else if (a == "--device-serial") {
                o.serial = next_value(argc, argv, i, "--device-serial");
            } else if (a == "--can-adapter") {
                o.adapter = next_value(argc, argv, i, "--can-adapter");
            } else if (a == "--dry-run") {
                o.dry_run = true;
                o.layer = Layer::Fake;
                o.duration_s = .5;
                o.warmup_s = .1;
                o.rounds = 1;
            } else {
                throw std::invalid_argument("unknown argument: " + a);
            }
        }
        if (o.help) {
            return o;
        }
        if (o.side != "left" && o.side != "right") {
            throw std::invalid_argument("--side must be left or right");
        }
        if (o.warmup_s < 0) {
            throw std::invalid_argument("--warmup must be non-negative");
        }
        if (o.data == DataKind::Angle && o.angle_hz <= 0) {
            throw std::invalid_argument("angle frequency required");
        }
        if (o.mode == Mode::Control && !o.confirm_motion) {
            throw std::invalid_argument(
                "control mode requires --confirm-motion (use it even for zero-amplitude hold tests)");
        }
        if (o.mode == Mode::Polling && o.angle_policy != AnglePolicy::QueryOnly) {
            throw std::invalid_argument("--angle-policy requires --mode control");
        }
        if (o.mode == Mode::Control && o.data == DataKind::Tactile) {
            throw std::invalid_argument("control mode requires angle data");
        }
        if (o.motion_amplitude < 0.0 || o.motion_amplitude > 100.0) {
            throw std::invalid_argument("--motion-amplitude must be in [0,100]");
        }
        if (o.motion_amplitude != 0.0) {
            throw std::invalid_argument(
                "O6 control benchmark currently supports only --motion-amplitude 0 (repeat current pose)");
        }
        return o;
    }

    uint32_t device_id(const Options &o) { return o.side == "left" ? kLeftId : kRightId; }
    bool wants_angle(const Options &o) { return o.data == DataKind::Angle || o.data == DataKind::Combined; }
    bool wants_tactile(const Options &o) { return o.data == DataKind::Tactile || o.data == DataKind::Combined; }
    std::chrono::microseconds period(double hz) {
        return std::chrono::microseconds{static_cast<int64_t>(std::llround(1'000'000.0 / hz))};
    }

#ifdef __linux__
    class FilteredSocketCan final : public linkerbot::can::CANTransport {
    public:
        FilteredSocketCan(const std::string &name, uint32_t id) {
            if (name.empty() || name.size() >= IFNAMSIZ) {
                throw std::invalid_argument("invalid CAN interface");
            }
            fd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
            if (fd_ < 0) {
                throw std::runtime_error("socket(PF_CAN) failed");
            }
            try {
                struct can_filter filter {};
                filter.can_id = id;
                filter.can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;
                if (::setsockopt(fd_, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter)) < 0) {
                    throw std::runtime_error("CAN_RAW_FILTER failed");
                }
                int own = 0;
                if (::setsockopt(fd_, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &own, sizeof(own)) < 0) {
                    throw std::runtime_error("CAN_RAW_RECV_OWN_MSGS failed");
                }
                struct ifreq req {};
                std::memcpy(req.ifr_name, name.data(), name.size());
                req.ifr_name[name.size()] = 0;
                if (::ioctl(fd_, SIOCGIFINDEX, &req) < 0) {
                    throw std::runtime_error("interface lookup failed");
                }
                struct sockaddr_can addr {};
                addr.can_family = AF_CAN;
                addr.can_ifindex = req.ifr_ifindex;
                if (::bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
                    throw std::runtime_error("CAN bind failed");
                }
                struct timeval timeout {
                    0, 10000
                };
                if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                    throw std::runtime_error("CAN timeout failed");
                }
            } catch (...) {
                ::close(fd_);
                fd_ = -1;
                throw;
            }
        }
        ~FilteredSocketCan() override { close(); }
        std::optional<Message> read() override {
            struct can_frame frame {};
            auto n = ::read(fd_, &frame, sizeof(frame));
            if (n == static_cast<ssize_t>(sizeof(frame))) {
                return linkerbot::can::decode_socketcan_frame(frame);
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return std::nullopt;
            }
            if (n < 0) {
                throw std::runtime_error("CAN read failed");
            }
            throw std::runtime_error("short CAN frame");
        }
        void write(const Message &m) override {
            auto bytes = linkerbot::can::encode_socketcan_frame(linkerbot::can::LinkKind::kClassic, m);
            auto n = ::write(fd_, bytes.data(), bytes.size());
            if (n != static_cast<ssize_t>(bytes.size())) {
                throw std::runtime_error("CAN write failed");
            }
        }
        void close() override {
            if (fd_ >= 0) {
                ::close(fd_);
                fd_ = -1;
            }
        }

    private:
        int fd_ = -1;
    };
#endif

    std::unique_ptr<linkerbot::can::CANTransport> make_transport(const Options &o) {
        if (o.layer == Layer::Fake) {
            auto fake = std::make_unique<linkerbot::can::FakeTransport>();
            const auto id = device_id(o);
            fake->on_request(id, [id](const Message &req) {
                std::vector<Message> out;
                if ((req.dlc == 1 || req.dlc == 7) && req.data[0] == 0x01) {
                    Message r{};
                    r.id = id;
                    r.dlc = 7;
                    r.data[0] = 0x01;
                    for (size_t i = 1; i < 7; ++i) {
                        r.data[i] = req.dlc == 7 ? req.data[i] : 128;
                    }
                    out.push_back(r);
                }
                if (req.dlc == 2 && req.data[1] == 0xA4 && req.data[0] >= 0xB1 && req.data[0] <= 0xB5) {
                    for (uint8_t row = 0; row < 10; ++row) {
                        Message r{};
                        r.id = id;
                        r.dlc = 6;
                        r.data[0] = req.data[0];
                        r.data[1] = static_cast<uint8_t>(row << 4);
                        for (size_t j = 0; j < 4; ++j) {
                            r.data[j + 2] = row;
                        }
                        out.push_back(r);
                    }
                }
                return out;
            });
            return fake;
        }
#ifdef __linux__
        return std::make_unique<FilteredSocketCan>(o.interface_name, device_id(o));
#else
        throw std::runtime_error("SocketCAN benchmark requires Linux");
#endif
    }

    class O6RawSession {
    public:
        using BeforeFinger = std::function<void(size_t, Clock::time_point)>;

        explicit O6RawSession(std::unique_ptr<linkerbot::can::CANTransport> transport, uint32_t id)
            : id_(id), dispatcher_(std::make_unique<linkerbot::can::CANDispatcher>(std::move(transport))) {
            subscription_ = dispatcher_->subscribe([this](const Message &m) { on_message(m); });
        }
        ~O6RawSession() {
            dispatcher_->unsubscribe(subscription_);
            dispatcher_->stop();
        }
        std::array<uint8_t, 6> query_angle(std::chrono::milliseconds timeout) {
            std::unique_lock<std::mutex> tx(tx_);
            return wait_angle(timeout);
        }
        std::array<uint8_t, 6> command_angle(const std::array<uint8_t, 6> &raw, bool expect_feedback,
                                             std::chrono::milliseconds timeout) {
            std::unique_lock<std::mutex> tx(tx_);
            if (expect_feedback) {
                return wait_angle(timeout, raw);
            }
            Message req{};
            req.id = id_;
            req.dlc = 7;
            req.data[0] = 0x01;
            std::copy(raw.begin(), raw.end(), req.data.begin() + 1);
            dispatcher_->send(req);
            return raw;
        }
        void query_tactile(std::chrono::milliseconds timeout, const BeforeFinger &before_finger = {}) {
            const auto deadline = Clock::now() + timeout;
            for (size_t finger = 0; finger < 5; ++finger) {
                if (before_finger) {
                    before_finger(finger, deadline);
                }
                std::unique_lock<std::mutex> tx(tx_);
                query_finger(static_cast<uint8_t>(0xB1 + finger), deadline);
            }
        }
        linkerbot::can::TransportHealth health() const { return dispatcher_->get_health(); }
        uint64_t protocol_errors() const { return protocol_errors_.load(); }
        uint64_t duplicates() const { return duplicates_.load(); }

    private:
        struct AnglePending {
            bool ready = false;
            std::array<uint8_t, 6> raw{};
            Clock::time_point sent{};
        };
        struct TactilePending {
            uint8_t prefix = 0;
            size_t count = 0;
            std::array<bool, 16> seen{};
            Clock::time_point sent{};
        };
        std::array<uint8_t, 6> wait_angle(std::chrono::milliseconds timeout,
                                          const std::optional<std::array<uint8_t, 6>> &command = {}) {
            {
                std::lock_guard<std::mutex> lock(state_);
                angle_ = AnglePending{};
                angle_->sent = Clock::now();
            }
            Message req{};
            req.id = id_;
            req.dlc = command ? 7 : 1;
            req.data[0] = 0x01;
            if (command) {
                std::copy(command->begin(), command->end(), req.data.begin() + 1);
            }
            dispatcher_->send(req);
            std::unique_lock<std::mutex> lock(state_);
            const auto deadline = Clock::now() + timeout;
            if (!cv_.wait_until(lock, deadline, [&] { return !angle_ || angle_->ready; }) || !angle_ ||
                !angle_->ready) {
                angle_.reset();
                timeout_count_.fetch_add(1);
                throw std::runtime_error("O6 angle timeout");
            }
            const auto raw = angle_->raw;
            angle_.reset();
            return raw;
        }
        void query_finger(uint8_t prefix, Clock::time_point deadline) {
            {
                std::lock_guard<std::mutex> lock(state_);
                tactile_ = TactilePending{};
                tactile_->prefix = prefix;
                tactile_->sent = Clock::now();
            }
            Message req{};
            req.id = id_;
            req.dlc = 2;
            req.data[0] = prefix;
            req.data[1] = 0xA4;
            dispatcher_->send(req);
            std::unique_lock<std::mutex> lock(state_);
            if (!cv_.wait_until(lock, deadline, [&] { return !tactile_ || tactile_->count >= 10; }) || !tactile_ ||
                tactile_->count < 10) {
                const auto count = tactile_ ? tactile_->count : 0;
                const auto timed_prefix = tactile_ ? tactile_->prefix : prefix;
                tactile_.reset();
                timeout_count_.fetch_add(1);
                throw std::runtime_error("O6 tactile timeout prefix=" + std::to_string(timed_prefix) +
                                         " rows=" + std::to_string(count));
            }
            tactile_.reset();
        }
        void on_message(const Message &m) {
            if (m.id != id_ || m.is_extended || m.is_fd || m.is_brs) {
                return;
            }
            std::lock_guard<std::mutex> lock(state_);
            if (m.data[0] == 0x01 && angle_) {
                if (m.dlc != 7) {
                    protocol_errors_.fetch_add(1);
                    return;
                }
                std::copy_n(m.data.begin() + 1, 6, angle_->raw.begin());
                angle_->ready = true;
                cv_.notify_all();
                return;
            }
            if (tactile_ && m.data[0] == tactile_->prefix) {
                if ((m.dlc != 6 && m.dlc != 7) || (m.data[1] & 0x0f) != 0) {
                    protocol_errors_.fetch_add(1);
                    if (!bad_frame_logged_.exchange(true)) {
                        std::cerr << "O6 tactile frame rejected: dlc=" << static_cast<int>(m.dlc) << " b0=0x"
                                  << std::hex << static_cast<int>(m.data[0]) << " b1=0x" << static_cast<int>(m.data[1])
                                  << " bytes=";
                        for (size_t i = 2; i < m.dlc; ++i) {
                            std::cerr << static_cast<int>(m.data[i]) << (i + 1 == m.dlc ? ' ' : ',');
                        }
                        std::cerr << std::dec << "\n";
                    }
                    return;
                }
                const auto row = static_cast<size_t>(m.data[1] >> 4);
                if (tactile_->seen[row]) {
                    duplicates_.fetch_add(1);
                    return;
                }
                tactile_->seen[row] = true;
                ++tactile_->count;
                if (tactile_->count >= 10) {
                    cv_.notify_all();
                }
            }
        }
        uint32_t id_;
        std::unique_ptr<linkerbot::can::CANDispatcher> dispatcher_;
        linkerbot::can::SubscriptionId subscription_{};
        mutable std::mutex state_;
        std::condition_variable cv_;
        std::mutex tx_;
        std::optional<AnglePending> angle_;
        std::optional<TactilePending> tactile_;
        std::atomic<uint64_t> timeout_count_{0}, protocol_errors_{0}, duplicates_{0}, bad_frame_logged_{false};
    };

    std::string json_escape(const std::string &s) {
        std::string out;
        for (unsigned char c : s) {
            if (c == '\\' || c == '"') {
                out += '\\';
                out += static_cast<char>(c);
            } else if (c < 0x20) {
                char b[7];
                std::snprintf(b, sizeof(b), "\\u%04x", c);
                out += b;
            } else {
                out += static_cast<char>(c);
            }
        }
        return out;
    }
    std::string now_iso() {
        auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
        gmtime_r(&t, &tm);
        std::ostringstream s;
        s << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return s.str();
    }
    void write_distribution(std::ostream &o, const Distribution &d) {
        o << "{\"count\":" << d.count << ",\"min_ms\":" << d.min_us / 1000 << ",\"mean_ms\":" << d.mean_us / 1000
          << ",\"p50_ms\":" << d.p50_us / 1000 << ",\"p95_ms\":" << d.p95_us / 1000 << ",\"p99_ms\":" << d.p99_us / 1000
          << ",\"max_ms\":" << d.max_us / 1000 << "}";
    }

    Result run_round(const Options &o) {
        Result result;
        result.requested_duration = o.duration_s;
        result.requested_angle = wants_angle(o) ? o.angle_hz : 0;
        result.requested_tactile = wants_tactile(o) ? o.tactile_hz : 0;
        result.requested_command = o.mode == Mode::Control ? o.command_hz : 0;
        auto counters = std::make_shared<Counters>();
        result.counters = counters;

        const auto started = Clock::now();
        const auto warmup_end =
            started + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(o.warmup_s));
        const auto end =
            warmup_end + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(o.duration_s));
        std::unique_ptr<O6RawSession> raw;
        std::unique_ptr<O6> sdk;
        const auto timeout = std::chrono::milliseconds{o.timeout_ms};
        if (o.layer == Layer::SocketCan || o.layer == Layer::Fake) {
            raw = std::make_unique<O6RawSession>(make_transport(o), device_id(o));
        } else {
            sdk = std::make_unique<O6>(o.side == "left" ? Side::kLeft : Side::kRight, o.interface_name);
        }

        std::array<uint8_t, 6> control_target{};
        if (o.mode == Mode::Control) {
            if (raw) {
                control_target = raw->query_angle(timeout);
            } else {
                control_target = sdk->angle.get_blocking(timeout).device_raw;
            }
            if (!o.confirm_motion) {
                throw std::runtime_error("control safety confirmation was not provided");
            }
        }

        auto next_angle = started;
        auto next_tactile = started;
        std::optional<Clock::time_point> last_command_feedback;
        const auto angle_period = period(o.angle_hz);
        const auto tactile_period = period(o.tactile_hz);
        static constexpr std::array<const char *, 5> kFingerNames{"thumb", "index", "middle", "ring", "pinky"};

        const auto record_angle = [&](Clock::time_point sent, Clock::time_point done, bool measured) {
            if (!measured || done >= end) {
                return;
            }
            const auto latency = std::chrono::duration<double, std::micro>(done - sent).count();
            std::lock_guard<std::mutex> lock(counters->value_mutex);
            counters->angle_completed.fetch_add(1);
            counters->angle_latency.push_back(latency);
            if (counters->last_angle != Clock::time_point{}) {
                counters->angle_intervals.push_back(
                    std::chrono::duration<double, std::micro>(done - counters->last_angle).count());
            }
            counters->last_angle = done;
        };

        const auto run_angle = [&]() {
            const auto now = Clock::now();
            if (now >= end) {
                return;
            }
            if (o.mode == Mode::Control && o.angle_policy == AnglePolicy::CommandFeedbackOnly) {
                return;
            }
            if (o.mode == Mode::Control && o.angle_policy == AnglePolicy::Adaptive && last_command_feedback &&
                *last_command_feedback >= next_angle) {
                next_angle += angle_period *
                              (now > next_angle ? static_cast<uint64_t>((now - next_angle) / angle_period) + 1 : 1);
                return;
            }
            const auto skipped = now > next_angle ? static_cast<uint64_t>((now - next_angle) / angle_period) : 0;
            next_angle += angle_period * (skipped + 1);
            const bool measured = now >= warmup_end;
            if (measured) {
                counters->angle_scheduled.fetch_add(1);
                counters->angle_sent.fetch_add(1);
                counters->angle_skipped.fetch_add(skipped);
            }
            const auto sent = Clock::now();
            if (raw) {
                (void)raw->query_angle(timeout);
            } else {
                (void)sdk->angle.get_blocking(timeout);
            }
            if (measured) {
                counters->query_angle.fetch_add(1);
            }

            record_angle(sent, Clock::now(), measured);
        };

        auto next_command = started;
        const auto command_period = period(o.command_hz);
        const auto run_command = [&]() {
            if (o.mode != Mode::Control || Clock::now() >= end) {
                return;
            }
            const auto now = Clock::now();
            const bool measured = now >= warmup_end;
            next_command += command_period *
                            (now > next_command ? static_cast<uint64_t>((now - next_command) / command_period) + 1 : 1);
            const auto sent = Clock::now();
            // O6 uses the same opcode and response shape for reads and writes and
            // has no documented reply-disable switch. Always consume the command
            // response so it cannot be mistaken for a following active query.
            const bool feedback = o.angle_policy != AnglePolicy::QueryOnly;
            if (raw) {
                (void)raw->command_angle(control_target, true, timeout);
            } else {
                (void)sdk->angle.set_angles_blocking(control_target, timeout);
            }
            if (measured) {
                counters->command_sent.fetch_add(1);
                if (feedback) {
                    counters->command_feedback_angle.fetch_add(1);
                    last_command_feedback = Clock::now();
                    record_angle(sent, Clock::now(), true);
                }
            }
        };

        const auto service_interstage = [&](size_t finger, Clock::time_point tactile_deadline) {
            if (!wants_angle(o) || finger == 0) {
                return;
            }
            for (unsigned dispatched = 0; dispatched < 8; ++dispatched) {
                const auto now = Clock::now();
                if (now >= end || now >= tactile_deadline) {
                    return;
                }
                const bool command_due = o.mode == Mode::Control && now >= next_command;
                const bool angle_due = now >= next_angle &&
                                       !(o.mode == Mode::Control && o.angle_policy == AnglePolicy::CommandFeedbackOnly);
                if (!command_due && !angle_due) {
                    return;
                }
                if (command_due && (!angle_due || next_command <= next_angle)) {
                    run_command();
                } else {
                    run_angle();
                }
            }
        };

        const auto run_tactile = [&]() {
            const auto now = Clock::now();
            const auto skipped = now > next_tactile ? static_cast<uint64_t>((now - next_tactile) / tactile_period) : 0;
            next_tactile += tactile_period * (skipped + 1);
            const bool measured = now >= warmup_end;
            if (measured) {
                counters->tactile_scheduled.fetch_add(1);
                counters->tactile_sent.fetch_add(1);
                counters->tactile_skipped.fetch_add(skipped);
            }
            const auto sent = Clock::now();
            const auto deadline = sent + timeout;
            if (raw) {
                raw->query_tactile(timeout, service_interstage);
            } else if (wants_angle(o)) {
                for (size_t finger = 0; finger < kFingerNames.size(); ++finger) {
                    service_interstage(finger, deadline);
                    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now());
                    if (remaining.count() <= 0) {
                        throw std::runtime_error("O6 tactile batch timeout");
                    }
                    (void)sdk->force_sensor.get_finger(kFingerNames[finger]).get_blocking(remaining);
                }
            } else {
                (void)sdk->force_sensor.get_blocking(timeout);
            }
            const auto done = Clock::now();
            if (!measured || done >= end) {
                return;
            }
            const auto latency = std::chrono::duration<double, std::micro>(done - sent).count();
            std::lock_guard<std::mutex> lock(counters->value_mutex);
            counters->tactile_completed.fetch_add(1);
            counters->tactile_latency.push_back(latency);
        };

        while (!g_stop.load()) {
            enum class Work { Command, Angle, Tactile };
            Work work = Work::Tactile;
            auto due = wants_tactile(o) ? next_tactile : Clock::time_point::max();
            if (o.mode == Mode::Control && next_command <= due) {
                work = Work::Command;
                due = next_command;
            }
            const bool angle_eligible =
                wants_angle(o) && !(o.mode == Mode::Control && o.angle_policy == AnglePolicy::CommandFeedbackOnly) &&
                !(o.mode == Mode::Control && o.angle_policy == AnglePolicy::Adaptive && o.command_hz >= o.angle_hz);
            if (angle_eligible && next_angle < due) {
                work = Work::Angle;
                due = next_angle;
            }
            if (due == Clock::time_point::max()) {
                break;
            }
            if (Clock::now() < due) {
                std::this_thread::sleep_until(due);
            }
            if (Clock::now() >= end) {
                break;
            }
            const bool measured = Clock::now() >= warmup_end;
            try {
                if (work == Work::Command) {
                    run_command();
                } else if (work == Work::Angle) {
                    run_angle();
                } else {
                    run_tactile();
                }
            } catch (const std::exception &error) {
                if (measured) {
                    if (counters->errors.fetch_add(1) == 0) {
                        std::cerr << "first transaction error: " << error.what() << "\n";
                    }
                    counters->timeout.fetch_add(1);
                }
            }
        }
        const auto actual_duration = o.duration_s;
        result.actual_duration = actual_duration;
        result.actual_angle = static_cast<double>(counters->angle_completed.load()) / std::max(actual_duration, 1e-9);
        result.actual_tactile =
            static_cast<double>(counters->tactile_completed.load()) / std::max(actual_duration, 1e-9);
        result.actual_command = static_cast<double>(counters->command_sent.load()) / std::max(actual_duration, 1e-9);
        {
            std::lock_guard<std::mutex> lock(counters->value_mutex);
            result.angle_latency = summarize(counters->angle_latency);
            result.tactile_latency = summarize(counters->tactile_latency);
            result.angle_interval = summarize(counters->angle_intervals);
        }
        if (raw) {
            const auto health = raw->health();
            result.pending = health.pending_tx;
            result.in_flight = health.pending_tx;
            counters->protocol_errors.fetch_add(raw->protocol_errors());
            counters->duplicates.fetch_add(raw->duplicates());
            result.transport_healthy = health.connected() && health.consecutive_errors == 0;
        } else {
            const auto health = sdk->runtime_health();
            result.pending = health.in_flight;
            result.in_flight = health.in_flight;
            counters->protocol_errors.fetch_add(health.protocol_errors);
            counters->timeout.fetch_add(health.timeouts);
            result.transport_healthy =
                health.state == linkerbot::hand::runtime::DeviceState::ready && health.polling_error.empty();
        }
        const auto fail = [&](const std::string &reason) {
            if (!result.failure.empty()) {
                result.failure += "; ";
            }
            result.failure += reason;
        };
        if (wants_angle(o) &&
            (result.actual_angle < o.angle_hz * .95 ||
             counters->angle_completed.load() <
                 static_cast<uint64_t>(static_cast<double>(counters->angle_scheduled.load()) * .95))) {
            fail("angle below 95%");
        }
        if (o.mode == Mode::Control &&
            counters->command_sent.load() < static_cast<uint64_t>(o.command_hz * o.duration_s * .95)) {
            fail("command submission rate below 95%");
        }
        if (wants_tactile(o) &&
            (result.actual_tactile < o.tactile_hz * .95 ||
             counters->tactile_completed.load() <
                 static_cast<uint64_t>(static_cast<double>(counters->tactile_scheduled.load()) * .95))) {
            fail("tactile below 95%");
        }
        if (counters->timeout.load()) {
            fail("timeout");
        }
        if (counters->errors.load() || counters->protocol_errors.load()) {
            fail("protocol/transport error");
        }
        if (!result.transport_healthy) {
            fail("transport unhealthy");
        }
        if (result.pending != 0 || result.in_flight != 0) {
            fail("pending transactions remain");
        }
        result.passed = result.failure.empty();
        return result;
    }

    void write_report(const Options &o, const std::vector<Result> &results) {
        if (o.output.empty()) {
            return;
        }
        std::filesystem::path path(o.output);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error("cannot open report: " + path.string());
        }
        out << std::fixed << std::setprecision(6) << "{\n  \"schema_version\": 1,\n  \"model\": \"O6\",\n"
            << "  \"hardware_verified\": " << (o.layer == Layer::Fake ? "false" : "true") << ",\n"
            << "  \"layer\": \""
            << (o.layer == Layer::Sdk         ? "sdk"
                : o.layer == Layer::SocketCan ? "socketcan"
                                              : "fake")
            << "\",\n"
            << "  \"side\": \"" << o.side << "\",\n  \"can_id\": " << device_id(o) << ",\n  \"interface\": \""
            << json_escape(o.interface_name) << "\",\n"
            << "  \"data\": \""
            << (o.data == DataKind::Angle     ? "angle"
                : o.data == DataKind::Tactile ? "tactile"
                                              : "combined")
            << "\",\n"
            << "  \"mode\": \"" << (o.mode == Mode::Control ? "control" : "polling") << "\",\n"
            << "  \"angle_policy\": \""
            << (o.angle_policy == AnglePolicy::QueryOnly             ? "query"
                : o.angle_policy == AnglePolicy::CommandFeedbackOnly ? "command-feedback"
                                                                     : "adaptive")
            << "\",\n"
            << "  \"command_hz\": " << o.command_hz << ",\n"
            << "  \"requested_angle_hz\": " << (wants_angle(o) ? o.angle_hz : 0) << ",\n"
            << "  \"requested_tactile_hz\": " << (wants_tactile(o) ? o.tactile_hz : 0) << ",\n"
            << "  \"duration_s\": " << o.duration_s << ",\n"
            << "  \"warmup_s\": " << o.warmup_s << ",\n"
            << "  \"firmware_version\": \"" << json_escape(o.firmware) << "\",\n"
            << "  \"sensor_version\": \"" << json_escape(o.sensor) << "\",\n"
            << "  \"device_serial_masked\": \"" << json_escape(o.serial) << "\",\n"
            << "  \"can_adapter\": \"" << json_escape(o.adapter) << "\",\n"
            << "  \"generated_at\": \"" << now_iso() << "\",\n  \"rounds\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto &r = results[i];
            const auto &c = *r.counters;
            out << "    {\"round\":" << r.round << ",\"actual_angle_hz\":" << r.actual_angle
                << ",\"actual_tactile_hz\":" << r.actual_tactile << ",\"actual_command_hz\":" << r.actual_command
                << ",\"angle_scheduled\":" << c.angle_scheduled.load()
                << ",\"angle_completed\":" << c.angle_completed.load()
                << ",\"angle_skipped\":" << c.angle_skipped.load() << ",\"query_angle_count\":" << c.query_angle.load()
                << ",\"command_sent_count\":" << c.command_sent.load()
                << ",\"command_feedback_angle_count\":" << c.command_feedback_angle.load()
                << ",\"tactile_scheduled\":" << c.tactile_scheduled.load()
                << ",\"tactile_completed\":" << c.tactile_completed.load()
                << ",\"tactile_skipped\":" << c.tactile_skipped.load() << ",\"timeout_count\":" << c.timeout.load()
                << ",\"protocol_error_count\":" << c.protocol_errors.load()
                << ",\"duplicate_count\":" << c.duplicates.load() << ",\"error_count\":" << c.errors.load()
                << ",\"pending_count\":" << r.pending << ",\"in_flight_count\":" << r.in_flight
                << ",\"transport_healthy\":" << (r.transport_healthy ? "true" : "false")
                << ",\"passed\":" << (r.passed ? "true" : "false") << ",\"failure_reason\":\"" << json_escape(r.failure)
                << "\"}" << (i + 1 == results.size() ? "" : ",") << "\n";
        }
        out << "  ]\n}\n";
    }

} // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    try {
        const auto options = parse_options(argc, argv);
        if (options.help) {
            usage();
            return 0;
        }
        std::vector<Result> results;
        for (int i = 0; i < options.rounds; ++i) {
            auto r = run_round(options);
            r.round = i + 1;
            results.push_back(std::move(r));
            const auto &last = results.back();
            const auto &c = *last.counters;
            std::cout << std::fixed << std::setprecision(2) << "round " << last.round << ": angle=" << last.actual_angle
                      << "Hz tactile=" << last.actual_tactile << "Hz command=" << last.actual_command << "Hz "
                      << (last.passed ? "PASS" : "FAIL") << (last.failure.empty() ? "" : " " + last.failure)
                      << " query_angle=" << c.query_angle.load() << " command_sent=" << c.command_sent.load()
                      << " command_feedback=" << c.command_feedback_angle.load() << "\n";
            if (i + 1 < options.rounds) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
        write_report(options, results);
        for (const auto &r : results) {
            if (!r.passed) {
                return 2;
            }
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "o6_frequency_benchmark: " << e.what() << "\n";
        return 1;
    }
}
