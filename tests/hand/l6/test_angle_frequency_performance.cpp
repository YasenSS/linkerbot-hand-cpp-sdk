#include "hand/support/temp_path.hpp"
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace {
    using Clock = std::chrono::steady_clock;

    std::atomic<bool> g_stop{false};

    void on_signal(int) { g_stop.store(true); }

    constexpr auto kSigintWait = std::chrono::seconds{20};
    constexpr auto kSigtermWait = std::chrono::seconds{5};
    constexpr auto kLayerGap = std::chrono::seconds{2};
    constexpr auto kPollSlice = std::chrono::milliseconds{20};

    std::optional<std::string> env_value(const char *name) {
        const char *value = std::getenv(name);
        if (value == nullptr) {
            return std::nullopt;
        }
        return std::string(value);
    }

    std::string env_or(const char *name, const std::string &fallback) {
        const auto value = env_value(name);
        if (!value.has_value()) {
            return fallback;
        }
        return *value;
    }

    bool is_hardware_layer(const std::string &layer) { return layer == "socketcan" || layer == "sdk"; }

    bool is_fake_layer(const std::string &layer) { return layer == "fake" || layer == "fake-socketcan"; }

    double parse_positive_double(const std::string &text, const std::string &name) {
        try {
            size_t consumed = 0;
            const double value = std::stod(text, &consumed);
            if (consumed != text.size() || !std::isfinite(value) || value <= 0.0) {
                throw std::invalid_argument(name);
            }
            return value;
        } catch (const std::exception &) {
            throw std::invalid_argument(name + " must be a positive number without trailing characters");
        }
    }

    double parse_nonnegative_double(const std::string &text, const std::string &name) {
        try {
            size_t consumed = 0;
            const double value = std::stod(text, &consumed);
            if (consumed != text.size() || !std::isfinite(value) || value < 0.0) {
                throw std::invalid_argument(name);
            }
            return value;
        } catch (const std::exception &) {
            throw std::invalid_argument(name + " must be a non-negative number without trailing characters");
        }
    }

    int parse_positive_int(const std::string &text, const std::string &name) {
        try {
            size_t consumed = 0;
            const int value = std::stoi(text, &consumed);
            if (consumed != text.size() || value <= 0) {
                throw std::invalid_argument(name);
            }
            return value;
        } catch (const std::exception &) {
            throw std::invalid_argument(name + " must be a positive integer without trailing characters");
        }
    }

    int parse_nonnegative_int(const std::string &text, const std::string &name) {
        try {
            size_t consumed = 0;
            const int value = std::stoi(text, &consumed);
            if (consumed != text.size() || value < 0) {
                throw std::invalid_argument(name);
            }
            return value;
        } catch (const std::exception &) {
            throw std::invalid_argument(name + " must be a non-negative integer without trailing characters");
        }
    }

    void require_range(double value, double min_inclusive, double max_inclusive, const std::string &name) {
        if (value < min_inclusive || value > max_inclusive) {
            throw std::invalid_argument(name + " is outside the allowed range");
        }
    }

    void require_range_int(int value, int min_inclusive, int max_inclusive, const std::string &name) {
        if (value < min_inclusive || value > max_inclusive) {
            throw std::invalid_argument(name + " is outside the allowed range");
        }
    }

    size_t skip_ws(const std::string &text, size_t index) {
        while (index < text.size() &&
               (text[index] == ' ' || text[index] == '\t' || text[index] == '\r' || text[index] == '\n')) {
            ++index;
        }
        return index;
    }

    size_t skip_json_string(const std::string &text, size_t index) {
        if (index >= text.size() || text[index] != '"') {
            throw std::runtime_error("expected JSON string");
        }
        ++index;
        while (index < text.size()) {
            if (static_cast<unsigned char>(text[index]) < 0x20) {
                throw std::runtime_error("unescaped JSON control character");
            }
            if (text[index] == '\\') {
                if (++index >= text.size()) {
                    throw std::runtime_error("incomplete JSON escape");
                }
                if (text[index] == 'u') {
                    for (unsigned digit = 0; digit < 4; ++digit) {
                        if (++index >= text.size() || std::isxdigit(static_cast<unsigned char>(text[index])) == 0) {
                            throw std::runtime_error("invalid JSON unicode escape");
                        }
                    }
                } else if (std::string("\"\\/bfnrt").find(text[index]) == std::string::npos) {
                    throw std::runtime_error("invalid JSON escape");
                }
                ++index;
                continue;
            }
            if (text[index] == '"') {
                return index + 1;
            }
            ++index;
        }
        throw std::runtime_error("unterminated JSON string");
    }

    size_t skip_json_value(const std::string &text, size_t index);

    size_t skip_json_container(const std::string &text, size_t index) {
        if (index >= text.size() || (text[index] != '{' && text[index] != '[')) {
            throw std::runtime_error("expected JSON container");
        }
        const bool object = text[index] == '{';
        const char close = object ? '}' : ']';
        index = skip_ws(text, index + 1);
        if (index < text.size() && text[index] == close) {
            return index + 1;
        }
        while (index < text.size()) {
            if (object) {
                index = skip_ws(text, skip_json_string(text, index));
                if (index >= text.size() || text[index] != ':') {
                    throw std::runtime_error("expected colon after JSON key");
                }
                ++index;
            }
            index = skip_ws(text, skip_json_value(text, index));
            if (index < text.size() && text[index] == close) {
                return index + 1;
            }
            if (index >= text.size() || text[index] != ',') {
                throw std::runtime_error("expected comma or JSON container end");
            }
            index = skip_ws(text, index + 1);
        }
        throw std::runtime_error("unbalanced JSON container");
    }

    size_t skip_json_value(const std::string &text, size_t index) {
        index = skip_ws(text, index);
        if (index >= text.size()) {
            throw std::runtime_error("expected JSON value");
        }
        if (text[index] == '"') {
            return skip_json_string(text, index);
        }
        if (text[index] == '{' || text[index] == '[') {
            return skip_json_container(text, index);
        }
        const auto begin = index;
        while (index < text.size()) {
            const char ch = text[index];
            if (ch == ',' || ch == '}' || ch == ']' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
                break;
            }
            ++index;
        }
        const auto token = text.substr(begin, index - begin);
        static const std::regex number{R"(-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?)"};
        if (token != "true" && token != "false" && token != "null" && !std::regex_match(token, number)) {
            throw std::runtime_error("invalid JSON value");
        }
        return index;
    }

    void validate_json_syntax(const std::string &text) {
        if (skip_ws(text, skip_json_value(text, 0)) != text.size()) {
            throw std::runtime_error("trailing content after JSON value");
        }
    }

    std::string decode_json_string_literal(const std::string &literal) {
        if (literal.size() < 2 || literal.front() != '"' || literal.back() != '"') {
            throw std::runtime_error("invalid JSON string literal");
        }
        std::string out;
        for (size_t index = 1; index + 1 < literal.size(); ++index) {
            if (literal[index] == '\\' && index + 2 < literal.size()) {
                ++index;
                out.push_back(literal[index]);
                continue;
            }
            out.push_back(literal[index]);
        }
        return out;
    }

    std::optional<std::string> find_member(const std::string &object, const std::string &key) {
        size_t index = skip_ws(object, 0);
        if (index >= object.size() || object[index] != '{') {
            return std::nullopt;
        }
        ++index;
        while (index < object.size()) {
            index = skip_ws(object, index);
            if (index < object.size() && object[index] == '}') {
                return std::nullopt;
            }
            if (index >= object.size() || object[index] != '"') {
                throw std::runtime_error("expected JSON object key");
            }
            const size_t key_begin = index;
            const size_t key_end = skip_json_string(object, index);
            const std::string seen = decode_json_string_literal(object.substr(key_begin, key_end - key_begin));
            index = skip_ws(object, key_end);
            if (index >= object.size() || object[index] != ':') {
                throw std::runtime_error("expected colon after JSON key");
            }
            ++index;
            index = skip_ws(object, index);
            const size_t value_begin = index;
            index = skip_json_value(object, index);
            if (seen == key) {
                return object.substr(value_begin, index - value_begin);
            }
            index = skip_ws(object, index);
            if (index < object.size() && object[index] == ',') {
                ++index;
            }
        }
        return std::nullopt;
    }

    std::string require_member(const std::string &object, const std::string &key) {
        const auto value = find_member(object, key);
        if (!value.has_value()) {
            throw std::runtime_error("missing JSON field '" + key + "'");
        }
        return *value;
    }

    std::string require_string_field(const std::string &object, const std::string &key) {
        return decode_json_string_literal(require_member(object, key));
    }

    bool require_bool_field(const std::string &object, const std::string &key) {
        const auto raw = require_member(object, key);
        if (raw == "true") {
            return true;
        }
        if (raw == "false") {
            return false;
        }
        throw std::runtime_error("JSON field '" + key + "' is not a boolean");
    }

    double require_double_field(const std::string &object, const std::string &key) {
        const auto raw = require_member(object, key);
        try {
            size_t consumed = 0;
            const size_t begin = skip_ws(raw, 0);
            const double value = std::stod(raw.substr(begin), &consumed);
            if (consumed != raw.size() - begin || !std::isfinite(value)) {
                throw std::invalid_argument(key);
            }
            return value;
        } catch (const std::exception &) {
            throw std::runtime_error("JSON field '" + key + "' is not a finite number");
        }
    }

    bool hz_close(double left, double right) { return std::fabs(left - right) <= 1e-4; }

    std::vector<std::string> parse_object_array(const std::string &array_text) {
        size_t index = skip_ws(array_text, 0);
        if (index >= array_text.size() || array_text[index] != '[') {
            throw std::runtime_error("expected JSON array");
        }
        ++index;
        std::vector<std::string> objects;
        while (true) {
            index = skip_ws(array_text, index);
            if (index < array_text.size() && array_text[index] == ']') {
                break;
            }
            if (index >= array_text.size() || array_text[index] != '{') {
                throw std::runtime_error("expected JSON object in array");
            }
            const size_t begin = index;
            index = skip_json_container(array_text, index);
            objects.push_back(array_text.substr(begin, index - begin));
            index = skip_ws(array_text, index);
            if (index < array_text.size() && array_text[index] == ',') {
                ++index;
            }
        }
        return objects;
    }

    struct PerfConfig {
        std::vector<std::string> layers;
        std::string mode;
        std::vector<std::string> policies;
        std::string phase;
        std::string interface_name;
        std::string side;
        std::string angle_hz;
        std::string command_hz;
        std::string duration_s;
        std::string warmup_s;
        std::string rounds;
        std::string search_min_hz;
        std::string search_max_hz;
        std::string search_step_hz;
        std::string search_resolution_hz;
        std::string final_duration_s;
        std::string final_rounds;
        std::string timeout_ms;
        std::string late_guard_ms;
        std::string firmware_version;
        std::string sensor_version;
        std::string device_serial_masked;
        std::string can_adapter;
        std::string topology;
        std::string candump_active;
        std::string initial_reply_state;
        std::filesystem::path output_dir;
        double angle_hz_value = 0.0;
        double command_hz_value = 0.0;
        double search_max_hz_value = 0.0;
        bool control = false;
        bool search = false;
    };

    std::vector<std::string> parse_layers(const std::string &text) {
        if (text == "socketcan" || text == "sdk" || text == "fake" || text == "fake-socketcan") {
            return {text};
        }
        if (text == "both") {
            return {"socketcan", "sdk"};
        }
        throw std::invalid_argument("L6_ANGLE_PERF_LAYER must be socketcan, sdk, both, fake, or "
                                    "fake-socketcan");
    }

    std::vector<std::string> parse_policies(const std::string &text, const std::string &mode) {
        if (text == "all") {
            if (mode != "control") {
                throw std::invalid_argument("L6_ANGLE_PERF_POLICY=all is only allowed with "
                                            "L6_ANGLE_PERF_MODE=control");
            }
            return {"query", "command-feedback", "adaptive"};
        }
        if (text == "query" || text == "command-feedback" || text == "adaptive") {
            return {text};
        }
        throw std::invalid_argument("L6_ANGLE_PERF_POLICY must be query, command-feedback, adaptive, "
                                    "or all");
    }

    PerfConfig parse_config() {
        PerfConfig config;
        config.layers = parse_layers(env_or("L6_ANGLE_PERF_LAYER", "both"));
        config.mode = env_or("L6_ANGLE_PERF_MODE", "polling");
        if (config.mode != "polling" && config.mode != "control") {
            throw std::invalid_argument("L6_ANGLE_PERF_MODE must be polling or control");
        }
        config.control = config.mode == "control";
        const std::string policy_text = env_or("L6_ANGLE_PERF_POLICY", "query");
        if (config.mode == "polling" && policy_text != "query") {
            throw std::invalid_argument("polling mode requires L6_ANGLE_PERF_POLICY=query; refusing to "
                                        "silently change the configured policy");
        }
        config.policies = parse_policies(policy_text, config.mode);
        config.phase = env_or("L6_ANGLE_PERF_PHASE", "smoke");
        if (config.phase != "smoke" && config.phase != "search") {
            throw std::invalid_argument("L6_ANGLE_PERF_PHASE must be smoke or search");
        }
        config.search = config.phase == "search";

        config.interface_name = env_or("CAN_INTERFACE", "can0");
        if (config.interface_name.empty() || !std::regex_match(config.interface_name, std::regex{"[A-Za-z0-9_.:-]+"})) {
            throw std::invalid_argument("CAN_INTERFACE contains unsafe characters");
        }
        config.side = env_or("L6_SIDE", "left");
        if (config.side != "left" && config.side != "right") {
            throw std::invalid_argument("L6_SIDE must be left or right");
        }

        config.angle_hz = env_or("L6_ANGLE_HZ", "10");
        config.command_hz = env_or("L6_COMMAND_HZ", "10");
        config.duration_s = env_or("L6_DURATION_S", "10");
        config.warmup_s = env_or("L6_WARMUP_S", "2");
        config.rounds = env_or("L6_ROUNDS", "1");
        config.search_min_hz = env_or("L6_SEARCH_MIN_HZ", "5");
        config.search_max_hz = env_or("L6_SEARCH_MAX_HZ", "200");
        config.search_step_hz = env_or("L6_SEARCH_STEP_HZ", "10");
        config.search_resolution_hz = env_or("L6_SEARCH_RESOLUTION_HZ", "1");
        config.final_duration_s = env_or("L6_FINAL_DURATION_S", "300");
        config.final_rounds = env_or("L6_FINAL_ROUNDS", "3");
        config.timeout_ms = env_or("L6_REQUEST_TIMEOUT_MS", "100");
        config.late_guard_ms = env_or("L6_LATE_GUARD_MS", "5");

        config.angle_hz_value = parse_positive_double(config.angle_hz, "L6_ANGLE_HZ");
        config.command_hz_value = parse_positive_double(config.command_hz, "L6_COMMAND_HZ");
        const double duration_s = parse_positive_double(config.duration_s, "L6_DURATION_S");
        const double warmup_s = parse_nonnegative_double(config.warmup_s, "L6_WARMUP_S");
        const int rounds = parse_positive_int(config.rounds, "L6_ROUNDS");
        const double search_min = parse_positive_double(config.search_min_hz, "L6_SEARCH_MIN_HZ");
        config.search_max_hz_value = parse_positive_double(config.search_max_hz, "L6_SEARCH_MAX_HZ");
        const double search_step = parse_positive_double(config.search_step_hz, "L6_SEARCH_STEP_HZ");
        const double search_resolution = parse_positive_double(config.search_resolution_hz, "L6_SEARCH_RESOLUTION_HZ");
        const double final_duration = parse_positive_double(config.final_duration_s, "L6_FINAL_DURATION_S");
        const int final_rounds = parse_positive_int(config.final_rounds, "L6_FINAL_ROUNDS");
        const int timeout_ms = parse_positive_int(config.timeout_ms, "L6_REQUEST_TIMEOUT_MS");
        const int late_guard_ms = parse_nonnegative_int(config.late_guard_ms, "L6_LATE_GUARD_MS");

        require_range(config.angle_hz_value, 0.0, 10000.0, "L6_ANGLE_HZ");
        require_range(config.command_hz_value, 0.0, 10000.0, "L6_COMMAND_HZ");
        require_range(duration_s, 0.0, 7.0 * 24.0 * 3600.0, "L6_DURATION_S");
        require_range(warmup_s, 0.0, 3600.0, "L6_WARMUP_S");
        require_range_int(rounds, 1, 10000, "L6_ROUNDS");
        require_range(search_min, 0.0, 10000.0, "L6_SEARCH_MIN_HZ");
        require_range(config.search_max_hz_value, 0.0, 10000.0, "L6_SEARCH_MAX_HZ");
        require_range(search_step, 0.0, 10000.0, "L6_SEARCH_STEP_HZ");
        require_range(search_resolution, 0.0, 10000.0, "L6_SEARCH_RESOLUTION_HZ");
        require_range(final_duration, 0.0, 7.0 * 24.0 * 3600.0, "L6_FINAL_DURATION_S");
        require_range_int(final_rounds, 1, 10000, "L6_FINAL_ROUNDS");
        require_range_int(timeout_ms, 1, 600000, "L6_REQUEST_TIMEOUT_MS");
        require_range_int(late_guard_ms, 0, 60000, "L6_LATE_GUARD_MS");
        if (search_min >= config.search_max_hz_value) {
            throw std::invalid_argument("L6_SEARCH_MIN_HZ must be lower than L6_SEARCH_MAX_HZ");
        }
        if (search_resolution >= search_step) {
            throw std::invalid_argument("L6_SEARCH_RESOLUTION_HZ must be lower than L6_SEARCH_STEP_HZ");
        }

        config.topology = env_or("L6_TOPOLOGY", "single");
        if (config.topology != "single" && config.topology != "dual") {
            throw std::invalid_argument("L6_TOPOLOGY must be single or dual");
        }
        config.candump_active = env_or("L6_CANDUMP_ACTIVE", "no");
        if (config.candump_active != "yes" && config.candump_active != "no") {
            throw std::invalid_argument("L6_CANDUMP_ACTIVE must be yes or no");
        }

        bool needs_hardware_metadata = false;
        for (const auto &layer : config.layers) {
            if (is_hardware_layer(layer)) {
                needs_hardware_metadata = true;
            }
        }
        config.firmware_version = env_or("L6_FIRMWARE_VERSION", "");
        config.sensor_version = env_or("L6_SENSOR_VERSION", "");
        config.device_serial_masked = env_or("L6_DEVICE_SERIAL_MASKED", "");
        config.can_adapter = env_or("L6_CAN_ADAPTER", "");
        if (needs_hardware_metadata && (config.firmware_version.empty() || config.sensor_version.empty() ||
                                        config.device_serial_masked.empty() || config.can_adapter.empty())) {
            throw std::invalid_argument("hardware layers require L6_FIRMWARE_VERSION, "
                                        "L6_SENSOR_VERSION, L6_DEVICE_SERIAL_MASKED, and "
                                        "L6_CAN_ADAPTER");
        }

        if (config.control) {
            const auto confirm = env_value("L6_ANGLE_PERF_CONFIRM_MOTION");
            if (!confirm.has_value() || *confirm != "YES_I_UNDERSTAND") {
                throw std::invalid_argument("control mode requires "
                                            "L6_ANGLE_PERF_CONFIRM_MOTION=YES_I_UNDERSTAND");
            }
            const auto reply = env_value("L6_INITIAL_REPLY_STATE");
            if (!reply.has_value() || (*reply != "on" && *reply != "off")) {
                throw std::invalid_argument("control mode requires L6_INITIAL_REPLY_STATE=on|off");
            }
            config.initial_reply_state = *reply;
        }

        for (const auto &policy : config.policies) {
            if (policy != "command-feedback") {
                continue;
            }
            // Search varies command_hz with each angle candidate in the
            // runner, so a fixed L6_COMMAND_HZ >= search-max is not required
            // and would not change per-candidate command bus load.
            if (!config.search && config.command_hz_value < config.angle_hz_value) {
                throw std::invalid_argument("command-feedback requires L6_COMMAND_HZ >= L6_ANGLE_HZ");
            }
        }

        config.output_dir = env_or("L6_ANGLE_PERF_OUTPUT_DIR", "tests/reports");
        if (config.output_dir.empty()) {
            throw std::invalid_argument("L6_ANGLE_PERF_OUTPUT_DIR is empty");
        }
        return config;
    }

    class SignalGuard {
    public:
        SignalGuard() {
            struct sigaction action {};
            action.sa_handler = on_signal;
            sigemptyset(&action.sa_mask);
            action.sa_flags = 0;
            if (sigaction(SIGINT, &action, &previous_int_) != 0 || sigaction(SIGTERM, &action, &previous_term_) != 0) {
                throw std::runtime_error("failed to install stop signal handlers");
            }
            armed_ = true;
        }

        ~SignalGuard() {
            if (!armed_) {
                return;
            }
            sigaction(SIGINT, &previous_int_, nullptr);
            sigaction(SIGTERM, &previous_term_, nullptr);
        }

        SignalGuard(const SignalGuard &) = delete;
        SignalGuard &operator=(const SignalGuard &) = delete;
        SignalGuard(SignalGuard &&) = delete;
        SignalGuard &operator=(SignalGuard &&) = delete;

    private:
        struct sigaction previous_int_ {};
        struct sigaction previous_term_ {};
        bool armed_ = false;
    };

    class ChildProcess {
    public:
        ChildProcess(const std::filesystem::path &binary, const std::vector<std::string> &args) { spawn(binary, args); }

        ~ChildProcess() {
            if (!reaped_ && pid_ > 0) {
                request_stop_and_reap();
            }
        }

        ChildProcess(const ChildProcess &) = delete;
        ChildProcess &operator=(const ChildProcess &) = delete;
        ChildProcess(ChildProcess &&) = delete;
        ChildProcess &operator=(ChildProcess &&) = delete;

        int wait_until_exit(Clock::time_point deadline = Clock::time_point::max()) {
            while (!reaped_) {
                if (Clock::now() >= deadline) {
                    throw std::runtime_error("runner exceeded test timeout");
                }
                if (g_stop.load()) {
                    request_stop_and_reap();
                    throw std::runtime_error("stop requested; runner received SIGINT for pose/0xCC "
                                             "restore");
                }
                int status = 0;
                const pid_t waited = ::waitpid(pid_, &status, WNOHANG);
                if (waited == pid_) {
                    status_ = status;
                    reaped_ = true;
                    pid_ = -1;
                    return status_;
                }
                if (waited < 0 && errno != EINTR) {
                    throw std::runtime_error(std::string("waitpid failed: ") + std::strerror(errno));
                }
                std::this_thread::sleep_for(kPollSlice);
            }
            return status_;
        }

    private:
        pid_t pid_ = -1;
        int status_ = 0;
        bool reaped_ = false;

        void spawn(const std::filesystem::path &binary, const std::vector<std::string> &args) {
            std::vector<char *> argv;
            argv.reserve(args.size() + 1);
            for (const auto &arg : args) {
                argv.push_back(const_cast<char *>(arg.c_str()));
            }
            argv.push_back(nullptr);

            posix_spawnattr_t attr;
            if (posix_spawnattr_init(&attr) != 0) {
                throw std::runtime_error("posix_spawnattr_init failed");
            }
            int rc = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
            if (rc == 0) {
                rc = posix_spawnattr_setpgroup(&attr, 0);
            }
            pid_t spawned = 0;
            if (rc == 0) {
                rc = posix_spawn(&spawned, binary.c_str(), nullptr, &attr, argv.data(), environ);
            }
            posix_spawnattr_destroy(&attr);
            if (rc != 0) {
                throw std::runtime_error(std::string("posix_spawn failed: ") + std::strerror(rc));
            }
            pid_ = spawned;
        }

        bool wait_for(std::chrono::milliseconds timeout) {
            const auto deadline = Clock::now() + timeout;
            while (Clock::now() < deadline) {
                int status = 0;
                const pid_t waited = ::waitpid(pid_, &status, WNOHANG);
                if (waited == pid_) {
                    status_ = status;
                    reaped_ = true;
                    pid_ = -1;
                    return true;
                }
                if (waited < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == ECHILD) {
                        reaped_ = true;
                        pid_ = -1;
                        return true;
                    }
                    return false;
                }
                std::this_thread::sleep_for(kPollSlice);
            }
            return false;
        }

        void request_stop_and_reap() {
            if (reaped_ || pid_ <= 0) {
                return;
            }
            (void)::kill(pid_, SIGINT);
            if (wait_for(kSigintWait)) {
                return;
            }
            (void)::kill(pid_, SIGTERM);
            if (wait_for(kSigtermWait)) {
                return;
            }
            (void)::kill(pid_, SIGKILL);
            int status = 0;
            while (true) {
                const pid_t waited = ::waitpid(pid_, &status, 0);
                if (waited == pid_ || (waited < 0 && errno == ECHILD)) {
                    status_ = status;
                    reaped_ = true;
                    pid_ = -1;
                    return;
                }
                if (waited < 0 && errno == EINTR) {
                    continue;
                }
                reaped_ = true;
                pid_ = -1;
                return;
            }
        }
    };

    std::filesystem::path read_self_exe() {
        std::array<char, 4096> buffer{};
        const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (length <= 0) {
            throw std::runtime_error("cannot read /proc/self/exe");
        }
        return std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(length)));
    }

    std::filesystem::path resolve_benchmark_binary() {
        if (const auto override_path = env_value("L6_ANGLE_PERF_BENCHMARK_BIN")) {
            if (override_path->empty()) {
                throw std::invalid_argument("L6_ANGLE_PERF_BENCHMARK_BIN is empty");
            }
            const std::filesystem::path path(*override_path);
            if (!std::filesystem::is_regular_file(path)) {
                throw std::runtime_error("L6_ANGLE_PERF_BENCHMARK_BIN is not a file: " + path.string());
            }
            return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
        }
        const auto candidate = read_self_exe().parent_path().parent_path() / "tools" / "l6_frequency_benchmark";
        if (!std::filesystem::is_regular_file(candidate)) {
            throw std::runtime_error("cannot find l6_frequency_benchmark at " + candidate.string() +
                                     "; build target l6_frequency_benchmark or set "
                                     "L6_ANGLE_PERF_BENCHMARK_BIN");
        }
        return candidate;
    }

    std::string utc_compact_now() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
        if (gmtime_r(&time, &utc) == nullptr) {
            throw std::runtime_error("gmtime_r failed");
        }
        std::ostringstream out;
        out << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
        return out.str();
    }

    std::filesystem::path allocate_output_path(const std::filesystem::path &directory, const std::string &stem) {
        std::filesystem::create_directories(directory);
        auto path = directory / (stem + ".json");
        if (!std::filesystem::exists(path)) {
            return std::filesystem::absolute(path);
        }
        for (int suffix = 1; suffix < 10000; ++suffix) {
            auto candidate = directory / (stem + "_" + std::to_string(suffix) + ".json");
            if (!std::filesystem::exists(candidate)) {
                return std::filesystem::absolute(candidate);
            }
        }
        throw std::runtime_error("cannot allocate a unique result file name");
    }

    std::string join_args(const std::vector<std::string> &args) {
        std::ostringstream out;
        for (size_t index = 0; index < args.size(); ++index) {
            if (index != 0) {
                out << ' ';
            }
            out << args[index];
        }
        return out.str();
    }

    std::string describe_wait_status(int status) {
        if (WIFEXITED(status)) {
            return "exit code " + std::to_string(WEXITSTATUS(status));
        }
        if (WIFSIGNALED(status)) {
            return "killed by signal " + std::to_string(WTERMSIG(status));
        }
        return "unknown wait status";
    }

    void wait_layer_gap() {
        const auto deadline = Clock::now() + kLayerGap;
        while (!g_stop.load() && Clock::now() < deadline) {
            std::this_thread::sleep_for(kPollSlice);
        }
    }

    std::string read_file(const std::filesystem::path &path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("cannot open result file");
        }
        std::ostringstream out;
        out << input.rdbuf();
        return out.str();
    }

    void require_round_fields(const std::string &round, bool require_schema_v3_fields) {
        const char *fields[] = {
            "requested_angle_hz",        "actual_angle_hz",  "scheduled_count",      "sent_count",
            "completed_count",           "timeout_count",    "protocol_error_count", "late_response_count",
            "forced_polling_stop_count", "event_drop_count", "unknown_origin_count", "pending_count",
            "in_flight_count",           "angle_latency",    "deadline_jitter",      "socketcan_delta",
            "transport_healthy",         "passed",
        };
        for (const char *field : fields) {
            (void)require_member(round, field);
        }
        if (require_schema_v3_fields) {
            for (const auto *field : {"angle_timeout_count", "tactile_timeout_count", "interstage_angle_timeout_count",
                                      "finger_0_timeout_count", "finger_1_timeout_count", "finger_2_timeout_count",
                                      "finger_3_timeout_count", "finger_4_timeout_count"}) {
                (void)require_member(round, field);
            }
        }
    }

    void validate_result_json(const std::string &json, const std::string &layer, const std::string &mode,
                              const std::string &policy, const std::string &phase) {
        if (json.empty()) {
            throw std::runtime_error("result file is empty");
        }
        validate_json_syntax(json);
        const auto schema = require_member(json, "schema_version");
        const size_t schema_begin = skip_ws(schema, 0);
        const auto schema_value = schema.substr(schema_begin);
        if (schema_value != "1" && schema_value != "2" && schema_value != "3") {
            throw std::runtime_error("schema_version must be 1, 2 or 3, got " + schema);
        }
        const bool schema_v3 = schema_value == "3";
        if (schema_v3) {
            (void)require_member(json, "interstage_request_timeout_us");
        }
        const bool hardware_verified = require_bool_field(json, "hardware_verified");
        if (is_hardware_layer(layer) && !hardware_verified) {
            throw std::runtime_error("hardware layer result must have hardware_verified=true");
        }
        if (is_fake_layer(layer) && hardware_verified) {
            throw std::runtime_error("fake layer result must not be marked hardware_verified");
        }
        if (require_string_field(json, "layer") != layer) {
            throw std::runtime_error("result layer does not match the scenario");
        }
        if (require_string_field(json, "mode") != mode) {
            throw std::runtime_error("result mode does not match the scenario");
        }
        if (require_string_field(json, "angle_policy") != policy) {
            throw std::runtime_error("result angle_policy does not match the scenario");
        }
        if (require_string_field(json, "data") != "angle") {
            throw std::runtime_error("result data is not angle");
        }

        const auto rounds = parse_object_array(require_member(json, "rounds"));
        if (rounds.empty()) {
            throw std::runtime_error("result contains no rounds");
        }
        bool all_rounds_passed = true;
        bool saw_final = false;
        for (const auto &round : rounds) {
            require_round_fields(round, schema_v3);
            const bool passed = require_bool_field(round, "passed");
            all_rounds_passed = all_rounds_passed && passed;
            if (require_string_field(round, "phase") == "final") {
                saw_final = true;
            }
            if (phase == "search" && policy == "command-feedback") {
                const double requested_angle = require_double_field(round, "requested_angle_hz");
                const double requested_command = require_double_field(round, "requested_command_hz");
                if (!hz_close(requested_angle, requested_command)) {
                    throw std::runtime_error("command-feedback search must vary command_hz with "
                                             "each angle candidate; requested_command_hz=" +
                                             require_member(round, "requested_command_hz") +
                                             " requested_angle_hz=" + require_member(round, "requested_angle_hz"));
                }
            }
        }

        const auto search = require_member(json, "search");
        const bool search_requested = require_bool_field(search, "requested");
        const bool bracketed = require_bool_field(search, "bracketed");
        const bool final_run = require_bool_field(search, "final_validation_run");
        const bool final_passed = require_bool_field(search, "final_validation_passed");
        const double winning_hz = require_double_field(search, "highest_candidate_pass_hz");
        (void)require_member(search, "first_fail_hz");

        if (phase == "smoke") {
            if (search_requested) {
                throw std::runtime_error("smoke result unexpectedly requested search");
            }
            if (!all_rounds_passed) {
                throw std::runtime_error("smoke result contains a failed round");
            }
            return;
        }

        if (!search_requested) {
            throw std::runtime_error("search result did not request search");
        }
        if (!bracketed) {
            if (all_rounds_passed) {
                throw std::runtime_error("搜索上界不足: every candidate passed, so search-max "
                                         "cannot be reported as a stable limit");
            }
            throw std::runtime_error("search did not bracket a passing point and a higher failing "
                                     "point");
        }
        if (!final_run || !saw_final) {
            throw std::runtime_error("search did not run final validation");
        }
        if (!final_passed) {
            throw std::runtime_error("search final validation did not find a stable candidate");
        }

        // Runner may fail 300s x 3 at the highest short-test candidate, then
        // step down and succeed. Only the winning candidate's final rounds
        // must all pass; earlier final attempts are allowed to fail.
        int winning_final_rounds = 0;
        for (const auto &round : rounds) {
            if (require_string_field(round, "phase") != "final") {
                continue;
            }
            if (!hz_close(require_double_field(round, "requested_angle_hz"), winning_hz)) {
                continue;
            }
            ++winning_final_rounds;
            if (!require_bool_field(round, "passed")) {
                throw std::runtime_error("winning final candidate has a failed round");
            }
        }
        if (winning_final_rounds <= 0) {
            throw std::runtime_error("final validation passed but no final rounds match "
                                     "highest_candidate_pass_hz");
        }
    }

    std::string sample_round_json(const std::string &phase, double angle_hz, bool passed, double command_hz) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6);
        out << "{" << "\"phase\":\"" << phase << "\"," << "\"requested_angle_hz\":" << angle_hz << ","
            << "\"actual_angle_hz\":" << angle_hz << "," << "\"requested_command_hz\":" << command_hz << ","
            << "\"scheduled_count\":1," << "\"sent_count\":1," << "\"completed_count\":1," << "\"timeout_count\":0,"
            << "\"protocol_error_count\":0," << "\"late_response_count\":0," << "\"forced_polling_stop_count\":0,"
            << "\"event_drop_count\":0," << "\"unknown_origin_count\":0," << "\"pending_count\":0,"
            << "\"in_flight_count\":0," << "\"angle_latency\":{\"count\":1}," << "\"deadline_jitter\":{\"count\":1},"
            << "\"socketcan_delta\":{\"available\":false}," << "\"transport_healthy\":true,"
            << "\"passed\":" << (passed ? "true" : "false") << "}";
        return out.str();
    }

    std::string sample_search_json(const std::string &mode, const std::string &policy, const std::string &rounds_json,
                                   double winning_hz) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6);
        out << "{" << "\"schema_version\":1," << "\"hardware_verified\":false," << "\"layer\":\"fake\","
            << "\"data\":\"angle\"," << "\"mode\":\"" << mode << "\"," << "\"angle_policy\":\"" << policy << "\","
            << "\"search\":{" << "\"requested\":true," << "\"bracketed\":true,"
            << "\"highest_candidate_pass_hz\":" << winning_hz << "," << "\"first_fail_hz\":" << (winning_hz + 10.0)
            << "," << "\"final_validation_run\":true," << "\"final_validation_passed\":true" << "}," << "\"rounds\":["
            << rounds_json << "]}";
        return out.str();
    }

    std::vector<std::string> build_argv(const std::filesystem::path &binary, const PerfConfig &config,
                                        const std::string &layer, const std::string &policy,
                                        const std::filesystem::path &output) {
        std::vector<std::string> args;
        args.push_back(binary.string());
        args.insert(args.end(), {"--layer", layer});
        args.insert(args.end(), {"--data", "angle"});
        args.insert(args.end(), {"--mode", config.mode});
        args.insert(args.end(), {"--angle-policy", policy});
        args.insert(args.end(), {"--side", config.side});
        args.insert(args.end(), {"--timeout-ms", config.timeout_ms});
        args.insert(args.end(), {"--guard-ms", config.late_guard_ms});
        args.insert(args.end(), {"--topology", config.topology});
        args.insert(args.end(), {"--candump-active", config.candump_active});
        args.insert(args.end(), {"--output", output.string()});
        args.insert(args.end(), {"--duration", config.duration_s});
        args.insert(args.end(), {"--warmup", config.warmup_s});

        if (is_hardware_layer(layer)) {
            args.insert(args.end(), {"--interface", config.interface_name});
            args.insert(args.end(), {"--firmware-version", config.firmware_version});
            args.insert(args.end(), {"--sensor-version", config.sensor_version});
            args.insert(args.end(), {"--device-serial", config.device_serial_masked});
            args.insert(args.end(), {"--can-adapter", config.can_adapter});
        }

        if (config.control) {
            args.push_back("--confirm-motion");
            args.insert(args.end(), {"--motion-amplitude", "0"});
            args.insert(args.end(), {"--initial-reply-state", config.initial_reply_state});
            args.insert(args.end(), {"--command-hz", config.command_hz});
        }

        if (config.search) {
            args.push_back("--search");
            args.insert(args.end(), {"--search-min", config.search_min_hz});
            args.insert(args.end(), {"--search-max", config.search_max_hz});
            args.insert(args.end(), {"--search-step", config.search_step_hz});
            args.insert(args.end(), {"--search-resolution", config.search_resolution_hz});
            args.insert(args.end(), {"--final-duration", config.final_duration_s});
            args.insert(args.end(), {"--final-rounds", config.final_rounds});
        } else {
            args.push_back("--single");
            args.insert(args.end(), {"--angle-hz", config.angle_hz});
            args.insert(args.end(), {"--rounds", config.rounds});
        }
        return args;
    }

    std::string scenario_name(const std::string &layer, const std::string &mode, const std::string &policy,
                              const std::string &phase) {
        return "L6/" + layer + "/" + mode + "/" + policy + "/" + phase;
    }

    void run_scenario(const std::filesystem::path &binary, const PerfConfig &config, const std::string &layer,
                      const std::string &policy) {
        const auto name = scenario_name(layer, config.mode, policy, config.phase);
        const auto stem = utc_compact_now() + "_L6_" + config.side + "_" + layer + "_" + config.mode + "_" + policy +
                          "_" + config.phase;
        const auto output = allocate_output_path(config.output_dir, stem);
        const auto args = build_argv(binary, config, layer, policy, output);

        UNSCOPED_INFO("scenario=" << name);
        UNSCOPED_INFO("result=" << output.string());
        UNSCOPED_INFO("command=" << join_args(args));
        std::cerr << "[l6-angle-frequency] scenario=" << name << " result=" << output.string() << "\n";

        if (g_stop.load()) {
            FAIL("scenario=" << name << " result=" << output.string() << " : stop requested before starting runner");
        }

        int status = 0;
        try {
            ChildProcess child(binary, args);
            status = child.wait_until_exit();
        } catch (const std::exception &error) {
            FAIL("scenario=" << name << " result=" << output.string() << " : " << error.what());
        }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            std::string extra;
            try {
                if (std::filesystem::exists(output) && std::filesystem::file_size(output) > 0) {
                    validate_result_json(read_file(output), layer, config.mode, policy, config.phase);
                }
            } catch (const std::exception &error) {
                extra = std::string(" ; JSON: ") + error.what();
            }
            FAIL("scenario=" << name << " result=" << output.string() << " : runner " << describe_wait_status(status)
                             << extra);
        }

        REQUIRE(std::filesystem::exists(output));
        REQUIRE(std::filesystem::file_size(output) > 0);
        REQUIRE_NOTHROW(validate_result_json(read_file(output), layer, config.mode, policy, config.phase));
    }
} // namespace

// JSON 检查拒绝裸控制字符、非法转义、缺失分隔符及尾随内容。
TEST_CASE("comm: L6 benchmark JSON validation rejects malformed output", "[comm][l6][performance][json]") {
    REQUIRE_NOTHROW(validate_json_syntax(R"({"text":"\u0000\t\r\n\"\\","values":[null,true,false,-1.25e+2,{}]})"));
    for (const auto *invalid :
         {"{\"text\":\"tab\there\"}", R"({"text":"\q"})", R"({"text":"\u00xz"})", R"({"a":1 "b":2})", R"({"a":1,})",
          "[1,]", "[true false]", "[01]", "[NaN]", "[1e]", "{} trailing", "\f{}"}) {
        CAPTURE(invalid);
        REQUIRE_THROWS_AS(validate_json_syntax(invalid), std::runtime_error);
    }
}

#ifdef LINKERBOT_L6_BENCHMARK_BINARY
// 通过现有 tests 目标执行两层模拟 runner，检查 JSON 转义、窗口计数与 P2 参数。
TEST_CASE("comm: L6 benchmark reports receive windows and P2 in valid JSON", "[comm][l6][performance][json]") {
    const std::filesystem::path binary{LINKERBOT_L6_BENCHMARK_BINARY};
    std::string metadata;
    // argv 不接受 NUL；覆盖其余全部 JSON 控制字符以及引号、反斜杠。
    for (char value = 1; value < 0x20; ++value) {
        metadata.push_back(value);
    }
    metadata += "quote\" slash\\";
    const std::string expected_metadata =
        R"json("\u0001\u0002\u0003\u0004\u0005\u0006\u0007\u0008\u0009\u000a\u000b)json"
        R"json(\u000c\u000d\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016)json"
        R"json(\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001fquote\" slash\\")json";
    g_stop.store(false);
    for (const auto *layer : {"fake", "fake-socketcan"}) {
        CAPTURE(layer);
        auto output = test_util::unique_mapping_path();
        output.replace_extension(".json");
        struct RemoveOutput {
            std::filesystem::path path;
            ~RemoveOutput() {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        } cleanup{output};
        const std::vector<std::string> args{binary.string(),
                                            "--layer",
                                            layer,
                                            "--data",
                                            "combined",
                                            "--angle-hz",
                                            "100",
                                            "--tactile-hz",
                                            "10",
                                            "--duration",
                                            "0.5",
                                            "--warmup",
                                            "0.05",
                                            "--query-lookahead",
                                            "--round-gap",
                                            "0",
                                            "--firmware-version",
                                            metadata,
                                            "--output",
                                            output.string()};
        ChildProcess child(binary, args);
        const auto status = child.wait_until_exit(Clock::now() + std::chrono::seconds{15});
        REQUIRE(WIFEXITED(status));
        REQUIRE(WEXITSTATUS(status) == 0);
        const auto json = read_file(output);
        REQUIRE_NOTHROW(validate_json_syntax(json));
        REQUIRE(require_member(json, "schema_version") == "3");
        REQUIRE(require_member(json, "firmware_version") == expected_metadata);
        REQUIRE(require_member(json, "scheduler_version") == R"("l6-interstage-v2")");
        REQUIRE(require_double_field(json, "interstage_request_timeout_us") == 5000);
        REQUIRE(require_bool_field(json, "query_lookahead"));
        REQUIRE_FALSE(require_bool_field(json, "hardware_verified"));
        const auto rounds = parse_object_array(require_member(json, "rounds"));
        REQUIRE(rounds.size() == 1);
        const auto &round = rounds.front();
        REQUIRE(require_bool_field(round, "passed"));
        REQUIRE(require_double_field(round, "angle_requested_period_count") == 50);
        REQUIRE(require_double_field(round, "tactile_requested_period_count") == 5);
        REQUIRE(require_double_field(round, "pending_count") == 0);
        REQUIRE(require_double_field(round, "in_flight_count") == 0);
        REQUIRE(require_double_field(round, "angle_timeout_count") == 0);
        REQUIRE(require_double_field(round, "tactile_timeout_count") == 0);
        REQUIRE(require_double_field(round, "interstage_angle_timeout_count") == 0);
        for (size_t finger = 0; finger < 5; ++finger) {
            REQUIRE(require_double_field(round, "finger_" + std::to_string(finger) + "_timeout_count") == 0);
        }
        REQUIRE(require_double_field(require_member(round, "angle_rx_interval"), "count") > 0);
        REQUIRE(require_double_field(require_member(round, "finger_4_latency"), "count") > 0);
        REQUIRE(hz_close(require_double_field(round, "actual_angle_hz"),
                         require_double_field(round, "angle_query_count") /
                             require_double_field(round, "actual_duration_s")));
    }

    ChildProcess invalid(binary, {binary.string(), "--layer", "fake", "--interstage-limit", "0", "--query-lookahead"});
    const auto status = invalid.wait_until_exit(Clock::now() + std::chrono::seconds{5});
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) != 0);

    ChildProcess invalid_timeout(binary,
                                 {binary.string(), "--layer", "fake", "--interstage-request-timeout-us", "999"});
    const auto timeout_status = invalid_timeout.wait_until_exit(Clock::now() + std::chrono::seconds{5});
    REQUIRE(WIFEXITED(timeout_status));
    REQUIRE(WEXITSTATUS(timeout_status) != 0);

    auto control_output = test_util::unique_mapping_path();
    control_output.replace_extension(".json");
    struct RemoveControlOutput {
        std::filesystem::path path;
        ~RemoveControlOutput() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } control_cleanup{control_output};
    const std::vector<std::string> control_args{binary.string(),
                                                "--layer",
                                                "fake-socketcan",
                                                "--data",
                                                "angle",
                                                "--mode",
                                                "control",
                                                "--angle-policy",
                                                "query",
                                                "--angle-hz",
                                                "50",
                                                "--command-hz",
                                                "50",
                                                "--duration",
                                                "0.5",
                                                "--warmup",
                                                "0.05",
                                                "--round-gap",
                                                "0",
                                                "--confirm-motion",
                                                "--motion-amplitude",
                                                "0",
                                                "--initial-reply-state",
                                                "off",
                                                "--output",
                                                control_output.string()};
    ChildProcess control(binary, control_args);
    const auto control_status = control.wait_until_exit(Clock::now() + std::chrono::seconds{15});
    REQUIRE(WIFEXITED(control_status));
    REQUIRE(WEXITSTATUS(control_status) == 0);
    const auto control_json = read_file(control_output);
    REQUIRE_NOTHROW(validate_json_syntax(control_json));
    const auto control_rounds = parse_object_array(require_member(control_json, "rounds"));
    REQUIRE(control_rounds.size() == 1);
    const auto &control_round = control_rounds.front();
    REQUIRE(require_bool_field(control_round, "passed"));
    REQUIRE(require_double_field(control_round, "angle_query_count") > 0);
    REQUIRE(require_double_field(control_round, "angle_command_feedback_count") > 0);
    REQUIRE(require_double_field(control_round, "timeout_count") == 0);
    REQUIRE(hz_close(require_double_field(control_round, "actual_angle_hz"),
                     require_double_field(control_round, "angle_query_count") /
                         require_double_field(control_round, "actual_duration_s")));
}
#endif

// L6 角度频率实机验收入口：安全编排已有 l6_frequency_benchmark，默认 SKIP，不打开 CAN。
TEST_CASE("L6 angle frequency performance acceptance", "[.l6-angle-frequency][l6][performance][angle]") {
    const auto enable = env_value("L6_ANGLE_PERF_ENABLE");
    if (!enable.has_value() || *enable != "1") {
        SKIP("L6_ANGLE_PERF_ENABLE is not 1; skipping without opening CAN or "
             "starting a runner");
    }

    g_stop.store(false);
    SignalGuard signals;
    PerfConfig config;
    std::filesystem::path binary;
    try {
        config = parse_config();
        binary = resolve_benchmark_binary();
    } catch (const std::exception &error) {
        FAIL(error.what());
    }

    for (size_t layer_index = 0; layer_index < config.layers.size(); ++layer_index) {
        if (layer_index > 0) {
            wait_layer_gap();
            if (g_stop.load()) {
                FAIL("stop requested during the inter-layer gap; refusing to "
                     "start a second I/O owner");
            }
        }
        for (const auto &policy : config.policies) {
            if (g_stop.load()) {
                FAIL("stop requested before scenario "
                     << scenario_name(config.layers[layer_index], config.mode, policy, config.phase));
            }
            run_scenario(binary, config, config.layers[layer_index], policy);
        }
    }
}

// 最终验证允许最高候选 300s×3 失败后降频成功；只要求获胜候选的 final 轮次通过。
TEST_CASE("search validation accepts stepped-down final winner", "[comm][l6][performance][angle]") {
    const auto rounds =
        sample_round_json("coarse", 10.0, true, 0.0) + "," + sample_round_json("coarse", 20.0, false, 0.0) + "," +
        sample_round_json("final", 20.0, false, 0.0) + "," + sample_round_json("final", 20.0, false, 0.0) + "," +
        sample_round_json("final", 20.0, false, 0.0) + "," + sample_round_json("final", 19.0, true, 0.0) + "," +
        sample_round_json("final", 19.0, true, 0.0) + "," + sample_round_json("final", 19.0, true, 0.0);
    REQUIRE_NOTHROW(validate_result_json(sample_search_json("polling", "query", rounds, 19.0), "fake", "polling",
                                         "query", "search"));
}

// 获胜候选自己的 final 轮次仍必须全部通过。
TEST_CASE("search validation rejects a failed winning final round", "[comm][l6][performance][angle]") {
    const auto rounds =
        sample_round_json("final", 19.0, true, 0.0) + "," + sample_round_json("final", 19.0, false, 0.0);
    REQUIRE_THROWS_AS(validate_result_json(sample_search_json("polling", "query", rounds, 19.0), "fake", "polling",
                                           "query", "search"),
                      std::runtime_error);
}

// command-feedback 搜索必须让 command_hz 跟随每个角度候选，而不是固定命令频率。
TEST_CASE("command-feedback search must vary command_hz with angle_hz", "[comm][l6][performance][angle]") {
    const auto coupled = sample_round_json("coarse", 10.0, true, 10.0) + "," +
                         sample_round_json("coarse", 20.0, false, 20.0) + "," +
                         sample_round_json("final", 10.0, true, 10.0);
    REQUIRE_NOTHROW(validate_result_json(sample_search_json("control", "command-feedback", coupled, 10.0), "fake",
                                         "control", "command-feedback", "search"));

    const auto uncoupled = sample_round_json("coarse", 10.0, true, 50.0) + "," +
                           sample_round_json("coarse", 20.0, false, 50.0) + "," +
                           sample_round_json("final", 10.0, true, 50.0);
    REQUIRE_THROWS_AS(validate_result_json(sample_search_json("control", "command-feedback", uncoupled, 10.0), "fake",
                                           "control", "command-feedback", "search"),
                      std::runtime_error);
}
