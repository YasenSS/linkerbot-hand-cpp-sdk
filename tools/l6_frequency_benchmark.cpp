#include "l6_frequency_benchmark_types.hpp"
#include "l6_raw_session.hpp"
#include "restore_state.hpp"
#include <linkerbot/hand/l6/detail/interstage.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l6/events.hpp>
#include <linkerbot/hand/l6/frame.hpp>
#include <linkerbot/hand/l6/l6.hpp>

namespace {
    using namespace l6_benchmark;
    using linkerbot::can::FakeTransport;
    using linkerbot::can::Message;
    using linkerbot::hand::l6::Angle;
    using linkerbot::hand::l6::AngleEvent;
    using linkerbot::hand::l6::AngleFeedbackPolicy;
    using linkerbot::hand::l6::ForceSensorEvent;
    using linkerbot::hand::l6::L6;
    using linkerbot::hand::l6::L6PollingPlan;
    using linkerbot::hand::l6::SampleOrigin;
    using linkerbot::hand::l6::Side;

    std::atomic<bool> g_stop{false};

    void on_signal(int) { g_stop.store(true); }

    void usage() {
        std::cout << "L6 angle/tactile frequency benchmark\n\n"
                  << "Usage: l6_frequency_benchmark --layer fake|fake-socketcan|socketcan|sdk [options]\n\n"
                  << "  --interface NAME              Required for socketcan/sdk\n"
                  << "  --side left|right             Device CAN ID: left=0x28, right=0x27\n"
                  << "  --data angle|tactile|combined\n"
                  << "  --mode polling|control\n"
                  << "  --angle-policy query|command-feedback|adaptive\n"
                  << "  --angle-hz HZ                 Angle target (or search variable)\n"
                  << "  --tactile-hz HZ               Five-finger sample target\n"
                  << "  --command-hz HZ               Required in control mode\n"
                  << "  --duration SEC --rounds N --warmup SEC\n"
                  << "  --timeout-ms N --guard-ms N --event-queue-size N\n"
                  << "  --interstage-limit N          Angle queries per finger boundary, 0=legacy (default 4)\n"
                  << "  --query-lookahead             Enable P2 for query-only polling\n"
                  << "  --finger-estimate-us N        Per-finger reservation (default 2500)\n"
                  << "  --timing-margin-us N          Reservation margin (default 100)\n"
                  << "  --interstage-angle-budget-us N  Nested query budget, >=1000 (default 2000)\n"
                  << "  --interstage-request-timeout-us N  Nested query hard timeout, >=1000 (default 5000)\n"
                  << "  --output FILE                 Machine-readable JSON output\n"
                  << "  --single                      Run only configured target (default)\n"
                  << "  --search                      Coarse + bisection + final validation\n"
                  << "                                (command-feedback also varies command-hz)\n"
                  << "  --search-min HZ --search-max HZ --search-step HZ\n"
                  << "  --search-resolution HZ\n"
                  << "  --final-duration SEC --final-rounds N (defaults 300 x 3)\n"
                  << "  --skip-final-validation       Search only; no stable-limit claim\n"
                  << "  --round-gap SEC               Default 2 seconds\n"
                  << "  --confirm-motion              Required for hardware control mode\n"
                  << "  --motion-amplitude PCT        0..10 around saved pose (default 0)\n"
                  << "  --initial-reply-state on|off  Required for hardware control mode;\n"
                  << "                                restored on every exit path\n"
                  << "  --firmware-version TEXT       Required hardware metadata\n"
                  << "  --sensor-version TEXT         Required hardware metadata\n"
                  << "  --device-serial MASKED        Required; do not store raw serials\n"
                  << "  --can-adapter TEXT            Required hardware metadata\n"
                  << "  --topology single|dual --candump-active yes|no\n"
                  << "  --dry-run                     Short fake single run\n"
                  << "  --help\n";
    }

    std::string value_after(int argc, char **argv, int &index, const std::string &option) {
        if (++index >= argc) {
            throw std::invalid_argument("missing value after " + option);
        }
        return argv[index];
    }

    struct ParsePositiveDoubleArguments {
        const std::string &text;
        const std::string &option;
    };
    double parse_positive_double(const ParsePositiveDoubleArguments &arguments) {
        size_t consumed = 0;
        const double value = std::stod(arguments.text, &consumed);
        if (consumed != arguments.text.size() || !std::isfinite(value) || value <= 0) {
            throw std::invalid_argument(arguments.option + " must be a positive number");
        }
        return value;
    }

    struct ParseNonnegativeDoubleArguments {
        const std::string &text;
        const std::string &option;
    };
    double parse_nonnegative_double(const ParseNonnegativeDoubleArguments &arguments) {
        size_t consumed = 0;
        const double value = std::stod(arguments.text, &consumed);
        if (consumed != arguments.text.size() || !std::isfinite(value) || value < 0) {
            throw std::invalid_argument(arguments.option + " must be a non-negative number");
        }
        return value;
    }

    struct ParsePositiveIntArguments {
        const std::string &text;
        const std::string &option;
    };
    int parse_positive_int(const ParsePositiveIntArguments &arguments) {
        size_t consumed = 0;
        const int value = std::stoi(arguments.text, &consumed);
        if (consumed != arguments.text.size() || value <= 0) {
            throw std::invalid_argument(arguments.option + " must be a positive integer");
        }
        return value;
    }

    Options parse_options(int argc, char **argv) {
        Options options;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            const auto next = [&]() { return value_after(argc, argv, index, argument); };
            if (argument == "--help" || argument == "-h") {
                options.help = true;
            } else if (argument == "--layer") {
                const auto value = next();
                if (value == "fake") {
                    options.layer = Layer::Fake;
                } else if (value == "fake-socketcan") {
                    options.layer = Layer::FakeSocketCan;
                } else if (value == "socketcan") {
                    options.layer = Layer::SocketCan;
                } else if (value == "sdk") {
                    options.layer = Layer::Sdk;
                } else {
                    throw std::invalid_argument("--layer must be fake, fake-socketcan, socketcan, or sdk");
                }
            } else if (argument == "--interface") {
                options.interface_name = next();
            } else if (argument == "--side") {
                options.side = next();
            } else if (argument == "--data") {
                const auto value = next();
                if (value == "angle") {
                    options.data = DataKind::Angle;
                } else if (value == "tactile") {
                    options.data = DataKind::Tactile;
                } else if (value == "combined") {
                    options.data = DataKind::Combined;
                } else {
                    throw std::invalid_argument("--data must be angle, tactile, or combined");
                }
            } else if (argument == "--mode") {
                const auto value = next();
                if (value == "polling") {
                    options.mode = Mode::Polling;
                } else if (value == "control") {
                    options.mode = Mode::Control;
                } else {
                    throw std::invalid_argument("--mode must be polling or control");
                }
            } else if (argument == "--angle-policy") {
                const auto value = next();
                if (value == "query") {
                    options.policy = AngleFeedbackPolicy::QueryOnly;
                } else if (value == "command-feedback") {
                    options.policy = AngleFeedbackPolicy::CommandFeedbackOnly;
                } else if (value == "adaptive") {
                    options.policy = AngleFeedbackPolicy::Adaptive;
                } else {
                    throw std::invalid_argument("--angle-policy must be query, command-feedback, or adaptive");
                }
            } else if (argument == "--interstage-limit") {
                const auto text = next();
                options.interstage.max_angle_transactions =
                    text == "0" ? 0 : static_cast<unsigned>(parse_positive_int({text, argument}));
            } else if (argument == "--query-lookahead") {
                options.interstage.query_lookahead = true;
            } else if (argument == "--finger-estimate-us") {
                options.interstage.finger_estimate = std::chrono::microseconds{parse_positive_int({next(), argument})};
            } else if (argument == "--timing-margin-us") {
                const auto text = next();
                options.interstage.timing_margin =
                    std::chrono::microseconds{text == "0" ? 0 : parse_positive_int({text, argument})};
            } else if (argument == "--interstage-angle-budget-us") {
                options.interstage.angle_budget = std::chrono::microseconds{parse_positive_int({next(), argument})};
            } else if (argument == "--interstage-request-timeout-us") {
                options.interstage.request_timeout = std::chrono::microseconds{parse_positive_int({next(), argument})};
            } else if (argument == "--angle-hz") {
                options.angle_hz = parse_positive_double({next(), "--angle-hz"});
            } else if (argument == "--tactile-hz") {
                options.tactile_hz = parse_positive_double({next(), "--tactile-hz"});
            } else if (argument == "--command-hz") {
                options.command_hz = parse_positive_double({next(), "--command-hz"});
            } else if (argument == "--duration") {
                options.duration_s = parse_positive_double({next(), "--duration"});
            } else if (argument == "--warmup") {
                options.warmup_s = parse_nonnegative_double({next(), "--warmup"});
            } else if (argument == "--rounds") {
                options.rounds = parse_positive_int({next(), "--rounds"});
            } else if (argument == "--timeout-ms") {
                options.timeout_ms = parse_positive_int({next(), "--timeout-ms"});
            } else if (argument == "--guard-ms") {
                options.late_guard_ms = static_cast<int>(parse_nonnegative_double({next(), "--guard-ms"}));
            } else if (argument == "--event-queue-size") {
                options.event_queue_size = static_cast<size_t>(parse_positive_int({next(), "--event-queue-size"}));
            } else if (argument == "--output") {
                options.output_path = next();
            } else if (argument == "--search") {
                options.search = true;
            } else if (argument == "--single") {
                options.search = false;
            } else if (argument == "--search-min") {
                options.search_min_hz = parse_positive_double({next(), "--search-min"});
            } else if (argument == "--search-max") {
                options.search_max_hz = parse_positive_double({next(), "--search-max"});
            } else if (argument == "--search-step") {
                options.search_step_hz = parse_positive_double({next(), "--search-step"});
            } else if (argument == "--search-resolution") {
                options.search_resolution_hz = parse_positive_double({next(), "--search-resolution"});
            } else if (argument == "--final-duration") {
                options.final_duration_s = parse_positive_double({next(), "--final-duration"});
            } else if (argument == "--final-rounds") {
                options.final_rounds = parse_positive_int({next(), "--final-rounds"});
            } else if (argument == "--skip-final-validation") {
                options.final_validation = false;
            } else if (argument == "--round-gap") {
                options.round_gap_s = parse_nonnegative_double({next(), "--round-gap"});
            } else if (argument == "--confirm-motion") {
                options.confirm_motion = true;
            } else if (argument == "--motion-amplitude") {
                options.motion_amplitude_percent = parse_nonnegative_double({next(), "--motion-amplitude"});
            } else if (argument == "--initial-reply-state") {
                const auto value = next();
                if (value == "on") {
                    options.initial_reply_enabled = true;
                } else if (value == "off") {
                    options.initial_reply_enabled = false;
                } else {
                    throw std::invalid_argument("--initial-reply-state must be on or off");
                }
            } else if (argument == "--firmware-version") {
                options.firmware_version = next();
            } else if (argument == "--sensor-version") {
                options.sensor_version = next();
            } else if (argument == "--device-serial") {
                options.device_serial = next();
            } else if (argument == "--can-adapter") {
                options.can_adapter = next();
            } else if (argument == "--topology") {
                options.topology = next();
            } else if (argument == "--candump-active") {
                options.candump_active = next();
            } else if (argument == "--dry-run") {
                options.dry_run = true;
                options.layer = Layer::Fake;
                options.duration_s = 0.5;
                options.warmup_s = 0.1;
                options.rounds = 1;
                options.search = false;
            } else {
                throw std::invalid_argument("unknown argument: " + argument);
            }
        }

        if (options.help) {
            return options;
        }
        if (options.side != "left" && options.side != "right") {
            throw std::invalid_argument("--side must be left or right");
        }
        if (options.layer != Layer::Fake && options.layer != Layer::FakeSocketCan && options.interface_name.empty()) {
            throw std::invalid_argument("--interface is required for socketcan/sdk");
        }
        if (!options.interface_name.empty() &&
            !std::regex_match(options.interface_name, std::regex{"[A-Za-z0-9_.:-]+"})) {
            throw std::invalid_argument("--interface contains unsafe characters");
        }
        linkerbot::hand::l6::detail::validate_interstage(options.interstage);
        if (options.interstage.query_lookahead &&
            (options.mode != Mode::Polling || options.policy != AngleFeedbackPolicy::QueryOnly ||
             options.interstage.max_angle_transactions == 0)) {
            throw std::invalid_argument(
                "--query-lookahead requires query-only polling and a positive interstage limit");
        }
        if (options.mode == Mode::Polling && options.policy != AngleFeedbackPolicy::QueryOnly) {
            throw std::invalid_argument("polling mode only supports --angle-policy query");
        }
        if (options.mode == Mode::Control && options.layer != Layer::Fake && options.layer != Layer::FakeSocketCan) {
            if (!options.confirm_motion) {
                throw std::invalid_argument("hardware control mode requires --confirm-motion");
            }
            if (!options.initial_reply_enabled) {
                throw std::invalid_argument("hardware control mode requires --initial-reply-state on|off "
                                            "so the modified 0xCC state can be restored");
            }
        }
        if (options.motion_amplitude_percent > 10.0) {
            throw std::invalid_argument("--motion-amplitude is limited to 10 percent");
        }
        if (options.topology != "single" && options.topology != "dual") {
            throw std::invalid_argument("--topology must be single or dual");
        }
        if (options.candump_active != "yes" && options.candump_active != "no") {
            throw std::invalid_argument("--candump-active must be yes or no");
        }
        if (options.layer != Layer::Fake && options.layer != Layer::FakeSocketCan &&
            (options.firmware_version.empty() || options.sensor_version.empty() || options.device_serial.empty() ||
             options.can_adapter.empty())) {
            throw std::invalid_argument("hardware runs require --firmware-version, "
                                        "--sensor-version, --device-serial (masked), "
                                        "and --can-adapter");
        }
        if (options.policy == AngleFeedbackPolicy::CommandFeedbackOnly && options.mode == Mode::Control &&
            !options.search && options.command_hz < options.angle_hz) {
            throw std::invalid_argument("command-feedback requires command-hz >= angle-hz");
        }
        if (options.search_min_hz >= options.search_max_hz) {
            throw std::invalid_argument("--search-min must be lower than --search-max");
        }
        if (options.search_resolution_hz >= options.search_step_hz) {
            throw std::invalid_argument("--search-resolution must be lower than --search-step");
        }
        return options;
    }

    uint32_t side_id(const Options &options) {
        return options.side == "left" ? linkerbot::hand::l6::kLeftArbitrationId
                                      : linkerbot::hand::l6::kRightArbitrationId;
    }

    Side side_value(const Options &options) { return options.side == "left" ? Side::kLeft : Side::kRight; }

    const char *layer_name(Layer layer) {
        if (layer == Layer::Fake) {
            return "fake";
        }
        if (layer == Layer::FakeSocketCan) {
            return "fake-socketcan";
        }
        if (layer == Layer::SocketCan) {
            return "socketcan";
        }
        return "sdk";
    }

    const char *data_name(DataKind data) {
        if (data == DataKind::Angle) {
            return "angle";
        }
        if (data == DataKind::Tactile) {
            return "tactile";
        }
        return "combined";
    }

    const char *policy_name(AngleFeedbackPolicy policy) {
        if (policy == AngleFeedbackPolicy::CommandFeedbackOnly) {
            return "command-feedback";
        }
        if (policy == AngleFeedbackPolicy::Adaptive) {
            return "adaptive";
        }
        return "query";
    }

    std::string iso_now() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
        gmtime_r(&time, &utc);
        std::ostringstream out;
        out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        return out.str();
    }

    std::string run_command(const std::string &command) {
        FILE *pipe = popen(command.c_str(), "r");
        if (pipe == nullptr) {
            return {};
        }
        std::string output;
        std::array<char, 512> buffer{};
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }
        (void)pclose(pipe);
        return output;
    }

    std::string trim_newlines(std::string value) {
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
            value.pop_back();
        }
        return value;
    }

    std::optional<uint64_t> read_counter(const std::string &interface_name, const char *counter) {
        std::ifstream input("/sys/class/net/" + interface_name + "/statistics/" + counter);
        uint64_t value = 0;
        if (!(input >> value)) {
            return std::nullopt;
        }
        return value;
    }

    InterfaceStats read_interface_stats(const Options &options) {
        InterfaceStats stats;
        if (options.layer == Layer::Fake || options.layer == Layer::FakeSocketCan) {
            return stats;
        }
        const auto rx_packets = read_counter(options.interface_name, "rx_packets");
        const auto tx_packets = read_counter(options.interface_name, "tx_packets");
        if (!rx_packets || !tx_packets) {
            return stats;
        }
        stats.available = true;
        stats.rx_packets = *rx_packets;
        stats.tx_packets = *tx_packets;
        stats.rx_errors = read_counter(options.interface_name, "rx_errors").value_or(0);
        stats.tx_errors = read_counter(options.interface_name, "tx_errors").value_or(0);
        stats.rx_dropped = read_counter(options.interface_name, "rx_dropped").value_or(0);
        stats.tx_dropped = read_counter(options.interface_name, "tx_dropped").value_or(0);
        stats.rx_over_errors = read_counter(options.interface_name, "rx_over_errors").value_or(0);
        stats.raw = run_command("ip -details -statistics link show dev " + options.interface_name + " 2>/dev/null");

        std::smatch match;
        if (std::regex_search(stats.raw, match, std::regex{"can state ([A-Z-]+)"})) {
            stats.state = match[1].str();
        }

        bool have_bus_errors = false;
        bool have_error_warning = false;
        bool have_error_passive = false;
        bool have_bus_off = false;
        std::istringstream lines(stats.raw);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.find("bus-errors") == std::string::npos || line.find("error-pass") == std::string::npos) {
                continue;
            }
            std::vector<std::string> labels;
            std::istringstream label_stream(line);
            std::string label;
            while (label_stream >> label) {
                labels.push_back(label);
            }
            std::string values_line;
            if (!std::getline(lines, values_line)) {
                break;
            }
            std::istringstream value_stream(values_line);
            for (const auto &name : labels) {
                uint64_t value = 0;
                if (!(value_stream >> value)) {
                    break;
                }
                if (name == "bus-errors") {
                    stats.bus_errors = value;
                    have_bus_errors = true;
                } else if (name == "error-warn") {
                    stats.error_warning = value;
                    have_error_warning = true;
                } else if (name == "error-pass") {
                    stats.error_passive = value;
                    have_error_passive = true;
                } else if (name == "bus-off") {
                    stats.bus_off = value;
                    have_bus_off = true;
                }
            }
            break;
        }
        stats.can_details_available =
            !stats.state.empty() && have_bus_errors && have_error_warning && have_error_passive && have_bus_off;
        return stats;
    }

    std::unique_ptr<linkerbot::can::CANTransport> make_fake_transport(uint32_t id) {
        auto transport = std::make_unique<FakeTransport>();
        auto reply_enabled = std::make_shared<std::atomic<bool>>(false);
        transport->on_request(id, [id, reply_enabled](const Message &request) {
            if (request.dlc == 7 && request.data[0] == linkerbot::hand::l6::kReplyOpcode) {
                reply_enabled->store(request.data[6] == 0x01);
                return std::vector<Message>{};
            }
            if (request.dlc >= 1 && request.data[0] == linkerbot::hand::l6::kAngleOpcode) {
                if (request.dlc == 7 && !reply_enabled->load()) {
                    return std::vector<Message>{};
                }
                Message response;
                response.id = id;
                response.dlc = 7;
                response.data[0] = linkerbot::hand::l6::kAngleOpcode;
                for (size_t index = 1; index < 7; ++index) {
                    response.data[index] = request.dlc == 7 ? request.data[index] : 0x40;
                }
                return std::vector<Message>{response};
            }
            if (request.dlc == 2 && request.data[1] == linkerbot::hand::l6::kForceSelectByte &&
                request.data[0] >= linkerbot::hand::l6::kThumbForceOpcode &&
                request.data[0] <= linkerbot::hand::l6::kPinkyForceOpcode) {
                std::vector<Message> responses;
                for (uint8_t row = 0; row < 12; ++row) {
                    Message response;
                    response.id = id;
                    response.dlc = 8;
                    response.data[0] = request.data[0];
                    response.data[1] = row;
                    response.data[2] = row;
                    responses.push_back(response);
                }
                return responses;
            }
            return std::vector<Message>{};
        });
        return transport;
    }

    struct AdvanceDeadlineArguments {
        Clock::time_point &deadline;
        std::chrono::microseconds period;
        Clock::time_point now;
        uint64_t &scheduled;
        uint64_t &skipped;
        std::vector<double> &jitters;
    };
    Clock::time_point advance_deadline(const AdvanceDeadlineArguments &arguments) {
        const auto due = arguments.deadline;
        const auto late = arguments.now - arguments.deadline;
        const auto periods = static_cast<uint64_t>(late / arguments.period) + 1;
        ++arguments.scheduled;
        if (periods > 1) {
            arguments.skipped += periods - 1;
        }
        arguments.jitters.push_back(std::chrono::duration<double, std::micro>(late).count());
        arguments.deadline += periods * arguments.period;
        return due;
    }

    void wait_duration(double seconds) {
        const auto end =
            Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
        while (!g_stop.load() && Clock::now() < end) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }

    Options apply_rate(Options options, double candidate_hz) {
        if (options.data == DataKind::Tactile) {
            options.tactile_hz = candidate_hz;
        } else {
            options.angle_hz = candidate_hz;
            // Command-feedback has no periodic queries; angle samples come
            // from command replies. Search must vary command_hz with the
            // candidate, otherwise every point has the same command bus load
            // and cannot bracket a meaningful feedback ceiling.
            if (options.mode == Mode::Control && options.policy == AngleFeedbackPolicy::CommandFeedbackOnly) {
                options.command_hz = candidate_hz;
            }
        }
        return options;
    }

    bool desired_reply_state(const Options &options) {
        // L6 0xCC gates all angle responses, including explicit queries.
        // Control mode must therefore keep replies enabled and consume each
        // command response. QueryOnly excludes those samples at evaluation
        // time instead of disabling them on the device.
        return options.mode == Mode::Control;
    }

    std::array<uint8_t, 6> target_raw(const std::array<uint8_t, 6> &initial, double amplitude_percent) {
        auto target = initial;
        const int offset = static_cast<int>(std::llround(amplitude_percent * 2.55));
        target[2] = static_cast<uint8_t>(std::clamp(static_cast<int>(target[2]) + offset, 0, 255));
        return target;
    }

    Metrics run_raw_window(RawSession &session, const Options &options, const std::array<uint8_t, 6> &command_target,
                           double duration_s) {
        session.reset_metrics();
        session.set_stop_predicate([] { return g_stop.load(); });
        struct WindowEnded {};
        Metrics scheduling;
        linkerbot::hand::l6::RuntimeStats boundary_stats;
        std::vector<double> jitters;
        const auto angle_period = period_for_hz(options.angle_hz);
        const auto tactile_period = period_for_hz(options.tactile_hz);
        const auto command_period = period_for_hz(options.command_hz);
        const auto timeout = std::chrono::milliseconds{options.timeout_ms};
        const auto started = Clock::now();
        const auto end =
            started + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(duration_s));
        session.set_measurement_window(started, end);
        auto angle_due = started;
        auto tactile_due = started;
        auto command_due = started;
        std::optional<Clock::time_point> last_command_feedback;
        const auto wait_until = [](Clock::time_point due) {
            while (!g_stop.load() && Clock::now() < due) {
                std::this_thread::sleep_until(std::min(due, Clock::now() + std::chrono::milliseconds{1}));
            }
        };
        const auto dispatch_angle = [&](std::chrono::milliseconds budget) {
            const auto now = Clock::now();
            if (!wants_angle(options.data) || now < angle_due || now >= end || g_stop.load()) {
                return false;
            }
            const auto due = advance_deadline({angle_due, angle_period, now, scheduling.angle_scheduled_count,
                                               scheduling.angle_skipped_deadline_count, jitters});
            bool query = options.policy == AngleFeedbackPolicy::QueryOnly;
            if (options.policy == AngleFeedbackPolicy::Adaptive) {
                query = linkerbot::hand::l6::detail::adaptive_requires_query(last_command_feedback, due);
            }
            if (!query) {
                return false;
            }
            const auto sample = session.query_angle(budget);
            boundary_stats.record_angle_deadline_latency(
                std::chrono::duration_cast<std::chrono::microseconds>(sample.timestamp - due));
            return true;
        };

        while (!g_stop.load() && Clock::now() < end) {
            const auto now = Clock::now();
            const bool due_command = options.mode == Mode::Control && now >= command_due;
            const bool due_angle = wants_angle(options.data) && now >= angle_due;
            const bool due_tactile = wants_tactile(options.data) && now >= tactile_due;
            if (due_command) {
                (void)advance_deadline({command_due, command_period, now, scheduling.command_scheduled_count,
                                        scheduling.command_skipped_deadline_count, jitters});
                try {
                    const auto sample = session.command_angle(command_target, desired_reply_state(options), timeout);
                    ++scheduling.command_sent_count;
                    ++scheduling.command_completed_count;
                    if (sample) {
                        last_command_feedback = sample->timestamp;
                    }
                } catch (const std::exception &) {
                    ++scheduling.thread_error_count;
                }
            }
            if (due_angle) {
                try {
                    (void)dispatch_angle(timeout);
                } catch (const std::exception &) {
                    ++scheduling.thread_error_count;
                }
            }
            if (due_tactile && !g_stop.load() && Clock::now() < end) {
                (void)advance_deadline({tactile_due, tactile_period, Clock::now(), scheduling.tactile_scheduled_count,
                                        scheduling.tactile_skipped_deadline_count, jitters});
                try {
                    const auto tactile_started = Clock::now();
                    const auto service_deadline = tactile_started + tactile_period;
                    (void)session.query_all_tactile(
                        std::chrono::milliseconds{std::max(options.timeout_ms, 1000)},
                        [&](size_t index, Clock::time_point hard_deadline) {
                            session.check_stopped();
                            if (Clock::now() >= end) {
                                throw WindowEnded{};
                            }
                            if (!session.health().connected()) {
                                throw std::runtime_error("Raw L6 transport disconnected");
                            }
                            if (!wants_angle(options.data) ||
                                options.policy == AngleFeedbackPolicy::CommandFeedbackOnly) {
                                return;
                            }
                            auto config = options.interstage;
                            config.query_lookahead =
                                config.query_lookahead && options.policy == AngleFeedbackPolicy::QueryOnly;
                            for (unsigned dispatched = 0; dispatched < config.max_angle_transactions;) {
                                session.check_stopped();
                                const auto at = Clock::now();
                                if (at >= end) {
                                    return;
                                }
                                const auto action = linkerbot::hand::l6::detail::boundary_action(
                                    config, at, angle_due, angle_period,
                                    std::min({hard_deadline, service_deadline, end}), 5 - index, dispatched);
                                using linkerbot::hand::l6::detail::BoundaryAction;
                                if (action == BoundaryAction::ContinueTactile) {
                                    return;
                                }
                                if (action == BoundaryAction::BudgetExhausted) {
                                    boundary_stats.add_interstage_budget_rejection_count();
                                    return;
                                }
                                if (action == BoundaryAction::WaitForAngle) {
                                    wait_until(std::min(angle_due, end));
                                    boundary_stats.add_interstage_wait_count();
                                    boundary_stats.record_interstage_wait(
                                        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - at));
                                    continue;
                                }
                                const auto budget = std::min(
                                    timeout, std::chrono::ceil<std::chrono::milliseconds>(config.request_timeout));
                                try {
                                    if (!dispatch_angle(budget)) {
                                        return;
                                    }
                                } catch (const std::exception &error) {
                                    if (std::string_view(error.what()) == "Raw L6 angle transaction timed out") {
                                        boundary_stats.inc_interstage_angle_timeout();
                                    }
                                    throw;
                                }
                                ++dispatched;
                                boundary_stats.add_interstage_angle_count();
                                boundary_stats.record_interstage_work(
                                    std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - at));
                            }
                        });
                    if (Clock::now() > service_deadline) {
                        boundary_stats.add_tactile_service_overrun_count();
                    }
                } catch (const WindowEnded &) {
                    break;
                } catch (const std::exception &) {
                    ++scheduling.thread_error_count;
                }
            }
            if (!due_command && !due_angle && !due_tactile) {
                auto next = end;
                if (options.mode == Mode::Control) {
                    next = std::min(next, command_due);
                }
                if (wants_angle(options.data)) {
                    next = std::min(next, angle_due);
                }
                if (wants_tactile(options.data)) {
                    next = std::min(next, tactile_due);
                }
                wait_until(next);
            }
        }

        auto metrics = session.metrics();
        const auto finger_latency = metrics.finger_latency;
        copy_scheduling_stats(metrics, boundary_stats.snapshot());
        metrics.finger_latency = finger_latency;
        metrics.command_scheduled_count = scheduling.command_scheduled_count;
        metrics.command_sent_count = scheduling.command_sent_count;
        metrics.command_completed_count = scheduling.command_completed_count;
        metrics.command_skipped_deadline_count = scheduling.command_skipped_deadline_count;
        metrics.angle_scheduled_count = scheduling.angle_scheduled_count;
        metrics.tactile_scheduled_count = scheduling.tactile_scheduled_count;
        metrics.angle_skipped_deadline_count = scheduling.angle_skipped_deadline_count;
        metrics.tactile_skipped_deadline_count = scheduling.tactile_skipped_deadline_count;
        metrics.skipped_deadline_count = metrics.angle_skipped_deadline_count + metrics.tactile_skipped_deadline_count;
        metrics.thread_error_count += scheduling.thread_error_count;
        metrics.deadline_jitter = summarize(jitters);
        metrics.scheduled_count = metrics.angle_scheduled_count + metrics.tactile_scheduled_count;
        metrics.measurement_duration_s =
            std::min(duration_s, std::chrono::duration<double>(Clock::now() - started).count());
        return metrics;
    }

    RoundResult evaluate(const Options &options, RoundResult result) {
        const auto ratio = [](uint64_t completed, uint64_t scheduled) {
            return scheduled == 0 ? 1.0 : static_cast<double>(completed) / static_cast<double>(scheduled);
        };
        if (options.mode == Mode::Control &&
            (ratio(result.metrics.command_sent_count, result.metrics.command_scheduled_count) < 0.95 ||
             result.actual_command_hz < options.command_hz * 0.95)) {
            result.failure_reasons.push_back("command submission rate is below 95% of target");
        }

        if (wants_angle(options.data)) {
            uint64_t accepted = result.metrics.angle_query_count;
            if (options.policy == AngleFeedbackPolicy::CommandFeedbackOnly) {
                accepted = result.metrics.angle_command_feedback_count;
            } else if (options.policy == AngleFeedbackPolicy::Adaptive) {
                accepted = result.metrics.angle_query_count + result.metrics.angle_command_feedback_count;
            }
            if (ratio(accepted, result.metrics.angle_scheduled_count) < 0.95 ||
                result.actual_angle_hz < options.angle_hz * 0.95) {
                result.failure_reasons.push_back("complete angle feedback is below 95% of target/scheduled");
            }
            if (options.policy == AngleFeedbackPolicy::CommandFeedbackOnly && result.metrics.angle_query_count != 0) {
                result.failure_reasons.push_back("command-feedback policy emitted active angle queries");
            }
            if (options.policy == AngleFeedbackPolicy::Adaptive && options.mode == Mode::Control &&
                options.command_hz >= options.angle_hz &&
                result.metrics.angle_command_feedback_count >=
                    static_cast<uint64_t>(0.95 * static_cast<double>(result.metrics.angle_scheduled_count)) &&
                result.metrics.angle_query_count > std::max<uint64_t>(2, result.metrics.angle_scheduled_count / 20)) {
                result.failure_reasons.push_back(
                    "adaptive issued excess queries while command feedback was sufficient");
            }
        }
        if (wants_tactile(options.data) &&
            (ratio(result.metrics.tactile_completed_count, result.metrics.tactile_scheduled_count) < 0.95 ||
             result.actual_tactile_hz < options.tactile_hz * 0.95)) {
            result.failure_reasons.push_back("complete five-finger tactile rate is below 95% of target/scheduled");
        }

        if (result.metrics.timeout_count != 0) {
            result.failure_reasons.push_back("timeout_count is non-zero");
        }
        if (result.metrics.protocol_error_count != 0) {
            result.failure_reasons.push_back("protocol_error_count is non-zero");
        }
        if (result.metrics.late_response_count != 0) {
            result.failure_reasons.push_back("late_response_count is non-zero");
        }
        if (result.metrics.conflicting_duplicate_count != 0) {
            result.failure_reasons.push_back("conflicting_duplicate_count is non-zero");
        }
        if (result.metrics.event_drop_count != 0) {
            result.failure_reasons.push_back("event_drop_count is non-zero");
        }
        if (result.metrics.unknown_origin_count != 0) {
            result.failure_reasons.push_back("unknown_origin_count is non-zero");
        }
        if (result.metrics.pending_count != 0 || result.metrics.in_flight_count != 0) {
            result.failure_reasons.push_back("pending/in-flight transactions remain after drain");
        }
        if (result.metrics.thread_error_count != 0) {
            result.failure_reasons.push_back("a workload task raised an exception");
        }
        if (result.metrics.forced_polling_stop_count != 0) {
            result.failure_reasons.push_back("graceful polling drain exceeded its deadline");
        }
        if (!result.transport_healthy) {
            result.failure_reasons.push_back("CAN dispatcher is not healthy after the round");
        }
        if (options.layer != Layer::Fake && options.layer != Layer::FakeSocketCan) {
            if (!result.interface_delta.available) {
                result.failure_reasons.push_back("SocketCAN interface statistics are unavailable");
            } else {
                if (!result.interface_delta.can_details_available) {
                    result.failure_reasons.push_back("SocketCAN CAN state/error counters are unavailable");
                } else if (result.interface_delta.error_counters_increased()) {
                    result.failure_reasons.push_back("SocketCAN error/drop counters increased");
                }
                if (result.interface_after.state != "ERROR-ACTIVE") {
                    result.failure_reasons.push_back("SocketCAN interface is not ERROR-ACTIVE");
                }
            }
        }
        if (g_stop.load()) {
            result.failure_reasons.push_back("measurement interrupted");
        }
        result.passed = result.failure_reasons.empty();
        return result;
    }

    RoundResult finish_result(const Options &options, RoundResult result, Clock::time_point started) {
        (void)started;
        result.actual_duration_s = result.metrics.measurement_duration_s;
        const auto period_count = [&](double hz) {
            const auto period = period_for_hz(hz);
            return static_cast<uint64_t>(
                std::ceil(result.actual_duration_s * 1e6 / static_cast<double>(period.count())));
        };
        result.metrics.angle_requested_period_count = wants_angle(options.data) ? period_count(options.angle_hz) : 0;
        result.metrics.tactile_requested_period_count =
            wants_tactile(options.data) ? period_count(options.tactile_hz) : 0;
        const double duration = std::max(result.actual_duration_s, 1e-9);
        uint64_t accepted_angle = result.metrics.angle_query_count;
        if (options.policy == AngleFeedbackPolicy::CommandFeedbackOnly) {
            accepted_angle = result.metrics.angle_command_feedback_count;
        } else if (options.policy == AngleFeedbackPolicy::Adaptive) {
            accepted_angle = result.metrics.angle_query_count + result.metrics.angle_command_feedback_count;
        }
        result.actual_angle_hz = static_cast<double>(accepted_angle) / duration;
        result.actual_tactile_hz = static_cast<double>(result.metrics.tactile_completed_count) / duration;
        result.actual_command_hz = static_cast<double>(result.metrics.command_sent_count) / duration;
        result.ended_at = iso_now();
        result.interface_after = read_interface_stats(options);
        result.interface_delta = difference(result.interface_before, result.interface_after);
        return evaluate(options, std::move(result));
    }

    RoundResult run_raw_round(const Options &options) {
        RoundResult result;
        result.requested_duration_s = options.duration_s;
        result.requested_angle_hz = wants_angle(options.data) ? options.angle_hz : 0.0;
        result.requested_tactile_hz = wants_tactile(options.data) ? options.tactile_hz : 0.0;
        result.requested_command_hz = options.mode == Mode::Control ? options.command_hz : 0.0;
        result.started_at = iso_now();
        result.interface_before = read_interface_stats(options);

        auto transport = options.layer == Layer::FakeSocketCan
                             ? make_fake_transport(side_id(options))
                             : make_socketcan_transport(options.interface_name, side_id(options));
        RawSession session(std::move(transport), side_id(options), std::chrono::milliseconds{options.late_guard_ms});
        const auto initial = session.query_angle(std::chrono::milliseconds{options.timeout_ms});
        const auto command_target = target_raw(initial.raw, options.motion_amplitude_percent);
        const bool restore_reply = options.initial_reply_enabled.value_or(false);
        const auto restore_device = [&]() {
            if (options.mode != Mode::Control) {
                return;
            }
            session.set_stop_predicate({});
            restore_state(
                [&] {
                    (void)session.command_angle(initial.raw, desired_reply_state(options),
                                                std::chrono::milliseconds{options.timeout_ms});
                },
                [&] { session.set_reply_enabled(restore_reply); });
        };

        try {
            if (options.mode == Mode::Control) {
                session.set_reply_enabled(desired_reply_state(options));
            }
            if (options.warmup_s > 0) {
                (void)run_raw_window(session, options, command_target, options.warmup_s);
            }
            const auto started = Clock::now();
            result.metrics = run_raw_window(session, options, command_target, options.duration_s);
            const auto health = session.health();
            result.transport_healthy = health.connected() && health.consecutive_errors == 0;
            restore_device();
            return finish_result(options, std::move(result), started);
        } catch (...) {
            if (options.mode == Mode::Control) {
                try {
                    restore_device();
                } catch (const std::exception &error) {
                    std::cerr << "Device restoration failed while preserving the original error: " << error.what()
                              << '\n';
                } catch (...) {
                    std::cerr
                        << "Device restoration failed with a non-standard exception; preserving the original error\n";
                }
            }
            throw;
        }
    }

    struct CommandCounters {
        std::atomic<uint64_t> scheduled{0};
        std::atomic<uint64_t> sent{0};
        std::atomic<uint64_t> completed{0};
        std::atomic<uint64_t> skipped{0};
        std::atomic<uint64_t> errors{0};
        std::mutex jitter_mutex;
        std::vector<double> jitters;
    };

    struct RunSdkMeasurementArguments {
        L6 &hand;
        const Options &options;
        const std::array<uint8_t, Angle::kJointCount> &hold;
        double warmup_s;
        double duration_s;
    };
    Metrics run_sdk_measurement(const RunSdkMeasurementArguments &arguments) {
        L6PollingPlan plan;
        plan.interstage = arguments.options.interstage;
        plan.angle_enabled = wants_angle(arguments.options.data);
        plan.tactile_enabled = wants_tactile(arguments.options.data);
        plan.angle_period = period_for_hz(arguments.options.angle_hz);
        plan.tactile_period = period_for_hz(arguments.options.tactile_hz);
        plan.angle_policy = arguments.options.policy;
        plan.request_timeout = std::chrono::milliseconds{arguments.options.timeout_ms};
        plan.tactile_timeout = std::chrono::milliseconds{std::max(arguments.options.timeout_ms, 1000)};
        plan.late_response_guard = std::chrono::milliseconds{arguments.options.late_guard_ms};

        std::atomic<bool> command_active{arguments.options.mode == Mode::Control};
        std::atomic<bool> measure_commands{false};
        CommandCounters command;
        std::thread commander;
        std::thread consumer;
        WindowSamples samples;
        Clock::time_point started;
        Clock::time_point end;
        try {
            arguments.hand.start_polling(plan);
            if (arguments.options.mode == Mode::Control) {
                commander = std::thread([&] {
                    const auto period = period_for_hz(arguments.options.command_hz);
                    auto due = Clock::now();
                    while (command_active.load() && !g_stop.load()) {
                        const auto now = Clock::now();
                        if (now < due) {
                            std::this_thread::sleep_until(due);
                        }
                        if (!command_active.load() || g_stop.load()) {
                            break;
                        }
                        const auto actual = Clock::now();
                        const auto periods = static_cast<uint64_t>((actual - due) / period) + 1;
                        const bool measured = measure_commands.load();
                        if (measured) {
                            command.scheduled.fetch_add(1);
                            if (periods > 1) {
                                command.skipped.fetch_add(periods - 1);
                            }
                            const std::scoped_lock lock(command.jitter_mutex);
                            command.jitters.push_back(std::chrono::duration<double, std::micro>(actual - due).count());
                        }
                        due += periods * period;
                        try {
                            const auto target = target_raw(arguments.hold, arguments.options.motion_amplitude_percent);
                            if (desired_reply_state(arguments.options)) {
                                (void)arguments.hand.angle.set_angles_blocking(
                                    target, std::chrono::milliseconds{arguments.options.timeout_ms});
                            } else {
                                arguments.hand.angle.set_angles(target);
                            }
                            if (measured) {
                                command.sent.fetch_add(1);
                                command.completed.fetch_add(1);
                            }
                        } catch (const std::exception &) {
                            if (measured) {
                                command.errors.fetch_add(1);
                            }
                        }
                    }
                });
            }
            wait_duration(arguments.warmup_s);
            auto &queue = arguments.hand.stream(arguments.options.event_queue_size);
            arguments.hand.reset_runtime_stats();
            started = Clock::now();
            end = started +
                  std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(arguments.duration_s));
            samples.begin = started;
            samples.end = end;
            consumer = std::thread([&] {
                for (const auto &event : queue) {
                    if (const auto *angle_event = std::get_if<AngleEvent>(&event)) {
                        if (angle_event->data.timestamp >= samples.begin) {
                            samples.angle(angle_event->data.timestamp, angle_event->data.origin);
                        }
                    }
                    if (const auto *force_event = std::get_if<ForceSensorEvent>(&event)) {
                        if (force_event->data.timestamp >= samples.begin) {
                            samples.force(force_event->data.timestamp);
                        }
                    }
                }
            });
            measure_commands.store(true);
            while (!g_stop.load() && Clock::now() < end) {
                std::this_thread::sleep_until(std::min(end, Clock::now() + std::chrono::milliseconds{1}));
            }
        } catch (...) {
            command_active.store(false);
            if (commander.joinable()) {
                commander.join();
            }
            arguments.hand.stop_polling();
            arguments.hand.stop_stream();
            if (consumer.joinable()) {
                consumer.join();
            }
            throw;
        }

        const auto window_ended = std::min(end, Clock::now());
        measure_commands.store(false);
        command_active.store(false);
        if (commander.joinable()) {
            commander.join();
        }
        const auto transaction_timeout_ms = wants_tactile(arguments.options.data)
                                                ? std::max(arguments.options.timeout_ms, 1000)
                                                : arguments.options.timeout_ms;
        const auto drain_timeout = std::chrono::milliseconds{transaction_timeout_ms + arguments.options.late_guard_ms};
        const bool drained = arguments.hand.stop_polling_gracefully(drain_timeout);
        arguments.hand.stop_stream();
        if (consumer.joinable()) {
            consumer.join();
        }

        const auto runtime = arguments.hand.get_runtime_stats();
        Metrics metrics;
        metrics.scheduled_count = runtime.scheduled_count;
        metrics.sent_count = runtime.sent_count;
        metrics.completed_count = runtime.completed_count;
        metrics.command_scheduled_count = command.scheduled.load();
        metrics.command_sent_count = command.sent.load();
        metrics.command_completed_count = command.completed.load();
        metrics.command_skipped_deadline_count = command.skipped.load();
        metrics.angle_scheduled_count = runtime.angle_scheduled_count;
        metrics.angle_query_count = runtime.query_angle_count;
        metrics.angle_command_feedback_count = runtime.command_feedback_angle_count;
        metrics.angle_feedback_count = runtime.angle_completed_count;
        metrics.angle_sent_count = runtime.angle_sent_count;
        metrics.tactile_scheduled_count = runtime.tactile_scheduled_count;
        metrics.tactile_completed_count = runtime.tactile_completed_count;
        metrics.tactile_sent_count = runtime.tactile_sent_count;
        metrics.timeout_count = runtime.timeout_count;
        metrics.angle_timeout_count = runtime.angle_timeout_count;
        metrics.tactile_timeout_count = runtime.tactile_timeout_count;
        metrics.tactile_finger_timeout_count = runtime.tactile_finger_timeout_count;
        metrics.protocol_error_count = runtime.protocol_error_count;
        metrics.late_response_count = runtime.late_response_count;
        metrics.duplicate_count = runtime.duplicate_count;
        metrics.conflicting_duplicate_count = runtime.conflicting_duplicate_count;
        metrics.skipped_deadline_count = runtime.skipped_deadline_count;
        metrics.event_drop_count = runtime.event_drop_count;
        metrics.pending_count = runtime.pending_count;
        metrics.in_flight_count = runtime.in_flight_count;
        const auto transport_health = arguments.hand.runtime()->session()->health();
        metrics.session_queue_high_water = transport_health.queue_high_water;
        metrics.session_rx_overflows = transport_health.overflows;
        metrics.unknown_origin_count = runtime.unknown_origin_count;
        metrics.thread_error_count = command.errors.load();
        metrics.forced_polling_stop_count = drained ? 0 : 1;
        samples.apply(metrics);
        copy_scheduling_stats(metrics, runtime);
        metrics.measurement_duration_s = std::chrono::duration<double>(window_ended - started).count();
        metrics.angle_latency = convert(runtime.angle_latency);
        metrics.tactile_latency = convert(runtime.tactile_latency);
        {
            const std::scoped_lock lock(command.jitter_mutex);
            auto jitters = command.jitters;
            const auto scheduler_jitter = convert(runtime.deadline_jitter);
            metrics.deadline_jitter = jitters.empty() ? scheduler_jitter : summarize(std::move(jitters));
        }
        return metrics;
    }

    RoundResult run_sdk_round(const Options &options) {
        RoundResult result;
        result.requested_duration_s = options.duration_s;
        result.requested_angle_hz = wants_angle(options.data) ? options.angle_hz : 0.0;
        result.requested_tactile_hz = wants_tactile(options.data) ? options.tactile_hz : 0.0;
        result.requested_command_hz = options.mode == Mode::Control ? options.command_hz : 0.0;
        result.started_at = iso_now();
        result.interface_before = read_interface_stats(options);

        std::unique_ptr<L6> hand;
        if (options.layer == Layer::Fake) {
            hand = std::make_unique<L6>(side_value(options), make_fake_transport(side_id(options)), "fake");
        } else {
            hand = std::make_unique<L6>(side_value(options), options.interface_name);
        }

        const auto initial = hand->angle.get_blocking(std::chrono::milliseconds{options.timeout_ms});
        const auto restore_reply = options.initial_reply_enabled.value_or(false);
        const auto restore_device = [&]() {
            if (options.mode != Mode::Control) {
                return;
            }
            restore_state(
                [&] {
                    if (desired_reply_state(options)) {
                        (void)hand->angle.set_angles_blocking(initial.device_raw,
                                                              std::chrono::milliseconds{options.timeout_ms});
                    } else {
                        hand->angle.set_angles(initial.device_raw);
                    }
                },
                [&] { hand->config.set_reply_enabled(restore_reply); });
        };
        try {
            if (options.mode == Mode::Control) {
                hand->config.set_reply_enabled(desired_reply_state(options));
            }
            const auto started = Clock::now();
            result.metrics =
                run_sdk_measurement({*hand, options, initial.device_raw, options.warmup_s, options.duration_s});
            const auto health = hand->get_transport_health();
            result.transport_healthy = health.connected() && health.consecutive_errors == 0;
            restore_device();
            return finish_result(options, std::move(result), started);
        } catch (...) {
            if (options.mode == Mode::Control) {
                try {
                    restore_device();
                } catch (const std::exception &error) {
                    std::cerr << "Device restoration failed while preserving the original error: " << error.what()
                              << '\n';
                } catch (...) {
                    std::cerr
                        << "Device restoration failed with a non-standard exception; preserving the original error\n";
                }
            }
            throw;
        }
    }

    RoundResult run_round(const Options &options) {
        if (options.layer == Layer::SocketCan || options.layer == Layer::FakeSocketCan) {
            return run_raw_round(options);
        }
        return run_sdk_round(options);
    }

    std::string join_reasons(const std::vector<std::string> &reasons) {
        std::ostringstream out;
        for (size_t index = 0; index < reasons.size(); ++index) {
            if (index != 0) {
                out << "; ";
            }
            out << reasons[index];
        }
        return out.str();
    }

    void print_result(const RoundResult &result) {
        std::cout << std::fixed << std::setprecision(2) << result.phase << " round " << result.round
                  << ": angle=" << result.actual_angle_hz << "Hz tactile=" << result.actual_tactile_hz
                  << "Hz command=" << result.actual_command_hz << "Hz timeout=" << result.metrics.timeout_count
                  << " protocol=" << result.metrics.protocol_error_count << " drop=" << result.metrics.event_drop_count
                  << (result.passed ? " PASS" : " FAIL") << "\n";
        if (!result.passed) {
            std::cout << "  " << join_reasons(result.failure_reasons) << "\n";
        }
    }

    std::string json_escape(const std::string &value) {
        static constexpr char hex[] = "0123456789abcdef";
        std::string escaped;
        for (unsigned char character : value) {
            if (character == '\\' || character == '"') {
                escaped.push_back('\\');
                escaped.push_back(static_cast<char>(character));
            } else if (character < 0x20) {
                escaped += "\\u00";
                escaped.push_back(hex[character >> 4]);
                escaped.push_back(hex[character & 15]);
            } else {
                escaped.push_back(static_cast<char>(character));
            }
        }
        return escaped;
    }

    void write_distribution(std::ostream &out, const char *name, const Distribution &value, bool comma = true) {
        out << "      \"" << name << "\": {\"count\": " << value.count << ", \"min_ms\": " << value.min_us / 1000.0
            << ", \"mean_ms\": " << value.mean_us / 1000.0 << ", \"p50_ms\": " << value.p50_us / 1000.0
            << ", \"p95_ms\": " << value.p95_us / 1000.0 << ", \"p99_ms\": " << value.p99_us / 1000.0
            << ", \"max_ms\": " << value.max_us / 1000.0 << "}" << (comma ? "," : "") << "\n";
    }

    void write_results(const Options &options, const std::vector<RoundResult> &results, const SearchSummary &search) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6);
        out << "{\n"
            << "  \"schema_version\": 3,\n"
            << "  \"hardware_verified\": "
            << ((options.layer == Layer::SocketCan || options.layer == Layer::Sdk) ? "true" : "false") << ",\n"
            << "  \"layer\": \"" << layer_name(options.layer) << "\",\n"
            << "  \"scheduler_version\": \"l6-interstage-v2\",\n"
            << "  \"interstage_limit\": " << options.interstage.max_angle_transactions << ",\n"
            << "  \"query_lookahead\": " << (options.interstage.query_lookahead ? "true" : "false") << ",\n"
            << "  \"finger_estimate_us\": " << options.interstage.finger_estimate.count() << ",\n"
            << "  \"timing_margin_us\": " << options.interstage.timing_margin.count() << ",\n"
            << "  \"interstage_angle_budget_us\": " << options.interstage.angle_budget.count() << ",\n"
            << "  \"interstage_request_timeout_us\": " << options.interstage.request_timeout.count() << ",\n"
            << "  \"model\": \"L6\",\n"
            << "  \"side\": \"" << options.side << "\",\n"
            << "  \"interface\": \"" << json_escape(options.interface_name) << "\",\n"
            << "  \"firmware_version\": \"" << json_escape(options.firmware_version) << "\",\n"
            << "  \"sensor_version\": \"" << json_escape(options.sensor_version) << "\",\n"
            << "  \"device_serial_masked\": \"" << json_escape(options.device_serial) << "\",\n"
            << "  \"can_adapter\": \"" << json_escape(options.can_adapter) << "\",\n"
            << "  \"topology\": \"" << options.topology << "\",\n"
            << "  \"candump_active\": \"" << options.candump_active << "\",\n"
            << "  \"data\": \"" << data_name(options.data) << "\",\n"
            << "  \"mode\": \"" << (options.mode == Mode::Control ? "control" : "polling") << "\",\n"
            << "  \"angle_policy\": \"" << policy_name(options.policy) << "\",\n"
            << "  \"sdk_commit\": \"" << L6_BENCHMARK_SDK_COMMIT << "\",\n"
            << "  \"sdk_dirty\": "
            << (std::string_view(L6_BENCHMARK_SDK_STATE) == "clean"      ? "false"
                : std::string_view(L6_BENCHMARK_SDK_STATE) == "modified" ? "true"
                                                                         : "null")
            << ",\n"
            << "  \"sdk_provenance\": \"CMake configure-time revision; archived builds use an explicit revision\",\n"
            << "  \"build_type\": \"" L6_BENCHMARK_BUILD_TYPE << "\",\n"
            << "  \"compiler\": \"" << json_escape(__VERSION__) << "\",\n"
            << "  \"system\": \"" << json_escape(trim_newlines(run_command("uname -srvmo 2>/dev/null"))) << "\",\n"
            << "  \"cpu\": \""
            << json_escape(trim_newlines(run_command("awk -F: '/model name/{sub(/^ /,\"\",$2); print $2; exit}' "
                                                     "/proc/cpuinfo 2>/dev/null")))
            << "\",\n"
            << "  \"load_average\": \"" << json_escape(trim_newlines(run_command("cat /proc/loadavg 2>/dev/null")))
            << "\",\n"
            << "  \"search\": {\"requested\": " << (search.requested ? "true" : "false")
            << ", \"bracketed\": " << (search.bracketed ? "true" : "false")
            << ", \"highest_candidate_pass_hz\": " << search.highest_candidate_pass_hz
            << ", \"first_fail_hz\": " << search.first_fail_hz
            << ", \"final_validation_run\": " << (search.final_validation_run ? "true" : "false")
            << ", \"final_validation_passed\": " << (search.final_validation_passed ? "true" : "false") << "},\n"
            << "  \"rounds\": [\n";
        for (size_t index = 0; index < results.size(); ++index) {
            const auto &result = results[index];
            const auto &metrics = result.metrics;
            out << "    {\n"
                << "      \"phase\": \"" << result.phase << "\",\n"
                << "      \"round\": " << result.round << ",\n"
                << "      \"started_at\": \"" << result.started_at << "\",\n"
                << "      \"ended_at\": \"" << result.ended_at << "\",\n"
                << "      \"requested_duration_s\": " << result.requested_duration_s << ",\n"
                << "      \"actual_duration_s\": " << result.actual_duration_s << ",\n"
                << "      \"requested_angle_hz\": " << result.requested_angle_hz << ",\n"
                << "      \"actual_angle_hz\": " << result.actual_angle_hz << ",\n"
                << "      \"requested_tactile_hz\": " << result.requested_tactile_hz << ",\n"
                << "      \"actual_tactile_hz\": " << result.actual_tactile_hz << ",\n"
                << "      \"requested_command_hz\": " << result.requested_command_hz << ",\n"
                << "      \"actual_command_hz\": " << result.actual_command_hz << ",\n"
                << "      \"actual_can_request_hz\": "
                << (static_cast<double>(metrics.sent_count) / std::max(result.actual_duration_s, 1e-9)) << ",\n"
                << "      \"actual_angle_send_hz\": "
                << (static_cast<double>(metrics.angle_sent_count) / std::max(result.actual_duration_s, 1e-9)) << ",\n"
                << "      \"actual_tactile_request_hz\": "
                << (static_cast<double>(metrics.tactile_sent_count) / std::max(result.actual_duration_s, 1e-9)) << ",\n"
                << "      \"scheduled_count\": " << metrics.scheduled_count << ",\n"
                << "      \"sent_count\": " << metrics.sent_count << ",\n"
                << "      \"completed_count\": " << metrics.completed_count << ",\n"
                << "      \"command_scheduled_count\": " << metrics.command_scheduled_count << ",\n"
                << "      \"command_sent_count\": " << metrics.command_sent_count << ",\n"
                << "      \"command_completed_count\": " << metrics.command_completed_count << ",\n"
                << "      \"command_skipped_deadline_count\": " << metrics.command_skipped_deadline_count << ",\n"
                << "      \"angle_scheduled_count\": " << metrics.angle_scheduled_count << ",\n"
                << "      \"angle_sent_count\": " << metrics.angle_sent_count << ",\n"
                << "      \"angle_query_count\": " << metrics.angle_query_count << ",\n"
                << "      \"angle_command_feedback_count\": " << metrics.angle_command_feedback_count << ",\n"
                << "      \"angle_feedback_count\": " << metrics.angle_feedback_count << ",\n"
                << "      \"tactile_scheduled_count\": " << metrics.tactile_scheduled_count << ",\n"
                << "      \"tactile_sent_count\": " << metrics.tactile_sent_count << ",\n"
                << "      \"tactile_completed_count\": " << metrics.tactile_completed_count << ",\n"
                << "      \"timeout_count\": " << metrics.timeout_count << ",\n"
                << "      \"angle_timeout_count\": " << metrics.angle_timeout_count << ",\n"
                << "      \"tactile_timeout_count\": " << metrics.tactile_timeout_count << ",\n"
                << "      \"interstage_angle_timeout_count\": " << metrics.interstage_angle_timeout_count << ",\n"
                << "      \"protocol_error_count\": " << metrics.protocol_error_count << ",\n"
                << "      \"late_response_count\": " << metrics.late_response_count << ",\n"
                << "      \"duplicate_count\": " << metrics.duplicate_count << ",\n"
                << "      \"conflicting_duplicate_count\": " << metrics.conflicting_duplicate_count << ",\n"
                << "      \"skipped_deadline_count\": " << metrics.skipped_deadline_count << ",\n"
                << "      \"event_drop_count\": " << metrics.event_drop_count << ",\n"
                << "      \"pending_count\": " << metrics.pending_count << ",\n"
                << "      \"in_flight_count\": " << metrics.in_flight_count << ",\n"
                << "      \"session_queue_high_water\": "
                << (metrics.session_queue_high_water ? std::to_string(*metrics.session_queue_high_water) : "null")
                << ",\n"
                << "      \"session_rx_overflows\": "
                << (metrics.session_rx_overflows ? std::to_string(*metrics.session_rx_overflows) : "null") << ",\n"
                << "      \"unknown_origin_count\": " << metrics.unknown_origin_count << ",\n";
            out << "      \"angle_skipped_deadline_count\": " << metrics.angle_skipped_deadline_count << ",\n";
            out << "      \"tactile_skipped_deadline_count\": " << metrics.tactile_skipped_deadline_count << ",\n";
            out << "      \"interstage_angle_count\": " << metrics.interstage_angle_count << ",\n";
            out << "      \"interstage_wait_count\": " << metrics.interstage_wait_count << ",\n";
            out << "      \"interstage_budget_rejection_count\": " << metrics.interstage_budget_rejection_count
                << ",\n";
            out << "      \"tactile_service_overrun_count\": " << metrics.tactile_service_overrun_count << ",\n";
            for (size_t finger = 0; finger < 5; ++finger) {
                out << "      \"finger_" << finger
                    << "_timeout_count\": " << metrics.tactile_finger_timeout_count[finger] << ",\n";
            }
            out << "      \"forced_polling_stop_count\": " << metrics.forced_polling_stop_count << ",\n";
            out << "      \"angle_requested_period_count\": " << metrics.angle_requested_period_count << ",\n";
            out << "      \"tactile_requested_period_count\": " << metrics.tactile_requested_period_count << ",\n";
            out << "      \"outside_angle_window_count\": " << metrics.outside_angle_window_count << ",\n";
            out << "      \"outside_tactile_window_count\": " << metrics.outside_tactile_window_count << ",\n";
            write_distribution(out, "angle_deadline_latency", metrics.angle_deadline_latency);
            write_distribution(out, "angle_rx_interval", metrics.angle_rx_interval);
            write_distribution(out, "interstage_wait", metrics.interstage_wait);
            write_distribution(out, "interstage_work", metrics.interstage_work);
            for (size_t finger = 0; finger < 5; ++finger) {
                const auto key = "finger_" + std::to_string(finger) + "_latency";
                write_distribution(out, key.c_str(), metrics.finger_latency[finger]);
            }
            write_distribution(out, "angle_latency", metrics.angle_latency);
            write_distribution(out, "tactile_latency", metrics.tactile_latency);
            write_distribution(out, "deadline_jitter", metrics.deadline_jitter);
            out << "      \"socketcan_delta\": {\"available\": "
                << (result.interface_delta.available ? "true" : "false")
                << ", \"can_details_available\": " << (result.interface_delta.can_details_available ? "true" : "false")
                << ", \"rx_packets\": " << result.interface_delta.rx_packets
                << ", \"tx_packets\": " << result.interface_delta.tx_packets
                << ", \"rx_errors\": " << result.interface_delta.rx_errors
                << ", \"tx_errors\": " << result.interface_delta.tx_errors
                << ", \"rx_dropped\": " << result.interface_delta.rx_dropped
                << ", \"tx_dropped\": " << result.interface_delta.tx_dropped
                << ", \"rx_over_errors\": " << result.interface_delta.rx_over_errors
                << ", \"bus_errors\": " << result.interface_delta.bus_errors
                << ", \"error_warning\": " << result.interface_delta.error_warning
                << ", \"error_passive\": " << result.interface_delta.error_passive
                << ", \"bus_off\": " << result.interface_delta.bus_off << "},\n"
                << "      \"socketcan_stats_before\": \"" << json_escape(result.interface_before.raw) << "\",\n"
                << "      \"socketcan_stats_after\": \"" << json_escape(result.interface_after.raw) << "\",\n"
                << "      \"transport_healthy\": " << (result.transport_healthy ? "true" : "false") << ",\n"
                << "      \"passed\": " << (result.passed ? "true" : "false") << ",\n"
                << "      \"failure_reason\": \"" << json_escape(join_reasons(result.failure_reasons)) << "\"\n"
                << "    }" << (index + 1 == results.size() ? "" : ",") << "\n";
        }
        out << "  ]\n}\n";

        if (options.output_path.empty()) {
            std::cout << out.str();
            return;
        }
        std::ofstream file(options.output_path);
        if (!file) {
            throw std::runtime_error("cannot write " + options.output_path);
        }
        file << out.str();
    }

    void gap(const Options &options) {
        if (!g_stop.load() && options.round_gap_s > 0) {
            wait_duration(options.round_gap_s);
        }
    }
} // namespace

int main(int argc, char **argv) {
    try {
        auto options = parse_options(argc, argv);
        if (options.help) {
            usage();
            return 0;
        }
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);

        std::vector<RoundResult> results;
        SearchSummary search;
        search.requested = options.search;
        if (!options.search) {
            for (int round = 1; round <= options.rounds && !g_stop.load(); ++round) {
                auto result = run_round(options);
                result.round = round;
                print_result(result);
                results.push_back(std::move(result));
                if (round != options.rounds) {
                    gap(options);
                }
            }
        } else {
            double highest_pass = 0.0;
            double first_fail = 0.0;
            for (double hz = options.search_min_hz; hz <= options.search_max_hz + 1e-9 && !g_stop.load();
                 hz += options.search_step_hz) {
                auto candidate = apply_rate(options, hz);
                auto result = run_round(candidate);
                result.phase = "coarse";
                print_result(result);
                results.push_back(result);
                if (result.passed) {
                    highest_pass = hz;
                    search.highest_candidate_pass_hz = hz;
                } else if (highest_pass > 0) {
                    first_fail = hz;
                    break;
                }
                gap(options);
            }

            if (highest_pass > 0 && first_fail > highest_pass) {
                gap(options);
                while (first_fail - highest_pass > options.search_resolution_hz && !g_stop.load()) {
                    const double hz = (highest_pass + first_fail) / 2.0;
                    auto candidate = apply_rate(options, hz);
                    auto result = run_round(candidate);
                    result.phase = "fine";
                    print_result(result);
                    results.push_back(result);
                    if (result.passed) {
                        highest_pass = hz;
                    } else {
                        first_fail = hz;
                    }
                    gap(options);
                }
                search.bracketed = true;
                search.highest_candidate_pass_hz = highest_pass;
                search.first_fail_hz = first_fail;
            }

            if (search.bracketed && options.final_validation && !g_stop.load()) {
                search.final_validation_run = true;
                bool stable = false;
                for (double candidate_hz = highest_pass;
                     candidate_hz >= options.search_min_hz && !stable && !g_stop.load();
                     candidate_hz -= options.search_resolution_hz) {
                    stable = true;
                    auto final_options = apply_rate(options, candidate_hz);
                    final_options.duration_s = options.final_duration_s;
                    for (int round = 1; round <= options.final_rounds && !g_stop.load(); ++round) {
                        auto result = run_round(final_options);
                        result.phase = "final";
                        result.round = round;
                        print_result(result);
                        stable = stable && result.passed;
                        results.push_back(std::move(result));
                        if (round != options.final_rounds) {
                            gap(options);
                        }
                    }
                    if (stable) {
                        search.highest_candidate_pass_hz = candidate_hz;
                    } else {
                        gap(options);
                    }
                }
                search.final_validation_passed = stable;
            }
        }

        write_results(options, results, search);
        if (options.layer == Layer::Fake || options.layer == Layer::FakeSocketCan) {
            std::cout << "Fake transport only: no hardware frequency limit was measured.\n";
        }

        bool passed = !results.empty();
        for (const auto &result : results) {
            passed = passed && result.passed;
        }
        if (options.search) {
            passed = search.bracketed && (!options.final_validation || search.final_validation_passed);
        }
        return passed ? 0 : 2;
    } catch (const std::invalid_argument &error) {
        std::cerr << "argument error: " << error.what() << "\n";
        usage();
        return 1;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
