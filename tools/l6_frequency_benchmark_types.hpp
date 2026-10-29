#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <linkerbot/hand/l6/angle.hpp>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include <linkerbot/hand/l6/runtime.hpp>

namespace l6_benchmark {
    using Clock = std::chrono::steady_clock;

    enum class Layer { Fake, FakeSocketCan, SocketCan, Sdk };

    enum class DataKind { Angle, Tactile, Combined };

    enum class Mode { Polling, Control };

    struct Options {
        Layer layer = Layer::Sdk;
        DataKind data = DataKind::Angle;
        Mode mode = Mode::Polling;
        linkerbot::hand::l6::AngleFeedbackPolicy policy = linkerbot::hand::l6::AngleFeedbackPolicy::QueryOnly;
        std::string interface_name;
        std::string side = "left";
        double angle_hz = 50.0;
        double tactile_hz = 10.0;
        double command_hz = 50.0;
        double duration_s = 15.0;
        double warmup_s = 2.0;
        int rounds = 1;
        int timeout_ms = 100;
        int late_guard_ms = 5;
        size_t event_queue_size = 4096;
        double motion_amplitude_percent = 0.0;
        bool confirm_motion = false;
        std::optional<bool> initial_reply_enabled;
        std::string firmware_version;
        std::string sensor_version;
        std::string device_serial;
        std::string can_adapter;
        std::string topology = "single";
        std::string candump_active = "no";
        std::string output_path;

        bool search = false;
        double search_min_hz = 5.0;
        double search_max_hz = 200.0;
        double search_step_hz = 10.0;
        double search_resolution_hz = 1.0;
        bool final_validation = true;
        double final_duration_s = 300.0;
        int final_rounds = 3;
        double round_gap_s = 2.0;
        bool dry_run = false;
        bool help = false;
        linkerbot::hand::l6::InterstageOptions interstage{};
    };

    struct Distribution {
        uint64_t count = 0;
        double min_us = 0.0;
        double mean_us = 0.0;
        double p50_us = 0.0;
        double p95_us = 0.0;
        double p99_us = 0.0;
        double max_us = 0.0;
    };

    inline Distribution summarize(std::vector<double> values) {
        Distribution out;
        if (values.empty()) {
            return out;
        }
        std::sort(values.begin(), values.end());
        const auto percentile = [&values](double fraction) {
            const auto index = static_cast<size_t>(fraction * static_cast<double>(values.size() - 1));
            return values[index];
        };
        out.count = values.size();
        out.min_us = values.front();
        out.mean_us = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        out.p50_us = percentile(0.50);
        out.p95_us = percentile(0.95);
        out.p99_us = percentile(0.99);
        out.max_us = values.back();
        return out;
    }

    inline Distribution convert(const linkerbot::hand::l6::RuntimeStatsSnapshot::Distribution &value) {
        return Distribution{value.count,  value.min_us, value.mean_us, value.p50_us,
                            value.p95_us, value.p99_us, value.max_us};
    }

    struct InterfaceStats {
        bool available = false;
        bool can_details_available = false;
        std::string state;
        uint64_t rx_packets = 0;
        uint64_t tx_packets = 0;
        uint64_t rx_errors = 0;
        uint64_t tx_errors = 0;
        uint64_t rx_dropped = 0;
        uint64_t tx_dropped = 0;
        uint64_t rx_over_errors = 0;
        uint64_t bus_errors = 0;
        uint64_t error_warning = 0;
        uint64_t error_passive = 0;
        uint64_t bus_off = 0;
        std::string raw;
    };

    struct InterfaceDelta {
        bool available = false;
        bool can_details_available = false;
        int64_t rx_packets = 0;
        int64_t tx_packets = 0;
        int64_t rx_errors = 0;
        int64_t tx_errors = 0;
        int64_t rx_dropped = 0;
        int64_t tx_dropped = 0;
        int64_t rx_over_errors = 0;
        int64_t bus_errors = 0;
        int64_t error_warning = 0;
        int64_t error_passive = 0;
        int64_t bus_off = 0;

        [[nodiscard]] bool error_counters_increased() const {
            return rx_errors > 0 || tx_errors > 0 || rx_dropped > 0 || tx_dropped > 0 || rx_over_errors > 0 ||
                   bus_errors > 0 || error_warning > 0 || error_passive > 0 || bus_off > 0;
        }
    };

    inline InterfaceDelta difference(const InterfaceStats &before, const InterfaceStats &after) {
        InterfaceDelta out;
        out.available = before.available && after.available;
        out.can_details_available = before.can_details_available && after.can_details_available;
        if (!out.available) {
            return out;
        }
        const auto diff = [](uint64_t lhs, uint64_t rhs) {
            return static_cast<int64_t>(rhs) - static_cast<int64_t>(lhs);
        };
        out.rx_packets = diff(before.rx_packets, after.rx_packets);
        out.tx_packets = diff(before.tx_packets, after.tx_packets);
        out.rx_errors = diff(before.rx_errors, after.rx_errors);
        out.tx_errors = diff(before.tx_errors, after.tx_errors);
        out.rx_dropped = diff(before.rx_dropped, after.rx_dropped);
        out.tx_dropped = diff(before.tx_dropped, after.tx_dropped);
        out.rx_over_errors = diff(before.rx_over_errors, after.rx_over_errors);
        out.bus_errors = diff(before.bus_errors, after.bus_errors);
        out.error_warning = diff(before.error_warning, after.error_warning);
        out.error_passive = diff(before.error_passive, after.error_passive);
        out.bus_off = diff(before.bus_off, after.bus_off);
        return out;
    }

    struct Metrics {
        uint64_t scheduled_count = 0;
        uint64_t sent_count = 0;
        uint64_t completed_count = 0;

        uint64_t command_scheduled_count = 0;
        uint64_t command_sent_count = 0;
        uint64_t command_completed_count = 0;
        uint64_t command_skipped_deadline_count = 0;

        uint64_t angle_scheduled_count = 0;
        uint64_t angle_query_count = 0;
        uint64_t angle_command_feedback_count = 0;
        uint64_t angle_feedback_count = 0;
        uint64_t angle_sent_count = 0;

        uint64_t tactile_scheduled_count = 0;
        uint64_t tactile_completed_count = 0;
        uint64_t tactile_sent_count = 0;

        uint64_t timeout_count = 0;
        uint64_t angle_timeout_count = 0;
        uint64_t tactile_timeout_count = 0;
        uint64_t interstage_angle_timeout_count = 0;
        std::array<uint64_t, 5> tactile_finger_timeout_count{};
        uint64_t protocol_error_count = 0;
        uint64_t late_response_count = 0;
        uint64_t duplicate_count = 0;
        uint64_t conflicting_duplicate_count = 0;
        uint64_t skipped_deadline_count = 0;
        uint64_t event_drop_count = 0;
        uint64_t pending_count = 0;
        uint64_t in_flight_count = 0;
        std::optional<uint64_t> session_queue_high_water;
        std::optional<uint64_t> session_rx_overflows;
        uint64_t unknown_origin_count = 0;
        uint64_t thread_error_count = 0;
        uint64_t forced_polling_stop_count = 0;

        Distribution angle_latency;
        Distribution tactile_latency;
        Distribution deadline_jitter;
        uint64_t angle_skipped_deadline_count = 0;
        uint64_t tactile_skipped_deadline_count = 0;
        uint64_t interstage_angle_count = 0;
        uint64_t interstage_wait_count = 0;
        uint64_t interstage_budget_rejection_count = 0;
        uint64_t tactile_service_overrun_count = 0;
        uint64_t angle_requested_period_count = 0;
        uint64_t tactile_requested_period_count = 0;
        uint64_t outside_angle_window_count = 0;
        uint64_t outside_tactile_window_count = 0;
        double measurement_duration_s = 0.0;
        Distribution angle_deadline_latency;
        Distribution angle_rx_interval;
        Distribution interstage_wait;
        Distribution interstage_work;
        std::array<Distribution, 5> finger_latency{};
    };

    // Receive-time window shared by raw and SDK observers. Drain is counted
    // separately even when the consumer sees an event after the window closes.
    struct WindowSamples {
        Clock::time_point begin = Clock::time_point::min();
        Clock::time_point end = Clock::time_point::max();
        uint64_t queries = 0, command_feedback = 0, unknown = 0, tactile = 0;
        uint64_t outside_angle = 0, outside_tactile = 0;
        std::optional<Clock::time_point> last_angle;
        std::vector<double> intervals_us;

        void angle(Clock::time_point received, linkerbot::hand::l6::SampleOrigin origin) {
            if (received < begin || received >= end) {
                ++outside_angle;
                return;
            }
            using linkerbot::hand::l6::SampleOrigin;
            if (origin == SampleOrigin::Query) {
                ++queries;
            } else if (origin == SampleOrigin::CommandFeedback) {
                ++command_feedback;
            } else {
                ++unknown;
            }
            if (last_angle) {
                intervals_us.push_back(std::chrono::duration<double, std::micro>(received - *last_angle).count());
            }
            last_angle = received;
        }
        void force(Clock::time_point received) {
            if (received >= begin && received < end) {
                ++tactile;
            } else {
                ++outside_tactile;
            }
        }
        void apply(Metrics &metrics) const {
            metrics.angle_query_count = queries;
            metrics.angle_command_feedback_count = command_feedback;
            metrics.angle_feedback_count = queries + command_feedback + unknown;
            metrics.unknown_origin_count = unknown;
            metrics.tactile_completed_count = tactile;
            metrics.outside_angle_window_count = outside_angle;
            metrics.outside_tactile_window_count = outside_tactile;
            metrics.angle_rx_interval = summarize(intervals_us);
        }
    };

    inline void copy_scheduling_stats(Metrics &out, const linkerbot::hand::l6::RuntimeStatsSnapshot &stats) {
        out.angle_skipped_deadline_count = stats.angle_skipped_deadline_count;
        out.tactile_skipped_deadline_count = stats.tactile_skipped_deadline_count;
        out.interstage_angle_count = stats.interstage_angle_count;
        out.interstage_wait_count = stats.interstage_wait_count;
        out.interstage_budget_rejection_count = stats.interstage_budget_rejection_count;
        out.tactile_service_overrun_count = stats.tactile_service_overrun_count;
        out.interstage_angle_timeout_count = stats.interstage_angle_timeout_count;
        out.angle_deadline_latency = convert(stats.angle_deadline_latency);
        out.interstage_wait = convert(stats.interstage_wait);
        out.interstage_work = convert(stats.interstage_work);
        for (size_t i = 0; i < 5; ++i) {
            out.finger_latency[i] = convert(stats.finger_latency[i]);
        }
    }

    struct RoundResult {
        std::string phase = "candidate";
        int round = 1;
        double requested_duration_s = 0.0;
        double actual_duration_s = 0.0;
        double requested_angle_hz = 0.0;
        double requested_tactile_hz = 0.0;
        double requested_command_hz = 0.0;
        double actual_angle_hz = 0.0;
        double actual_tactile_hz = 0.0;
        double actual_command_hz = 0.0;
        std::string started_at;
        std::string ended_at;
        Metrics metrics;
        InterfaceStats interface_before;
        InterfaceStats interface_after;
        InterfaceDelta interface_delta;
        bool transport_healthy = true;
        bool passed = false;
        std::vector<std::string> failure_reasons;
    };

    struct SearchSummary {
        bool requested = false;
        bool bracketed = false;
        bool final_validation_run = false;
        bool final_validation_passed = false;
        double highest_candidate_pass_hz = 0.0;
        double first_fail_hz = 0.0;
    };

    inline std::chrono::microseconds period_for_hz(double hz) {
        return std::chrono::microseconds{std::max<int64_t>(1, static_cast<int64_t>(std::llround(1'000'000.0 / hz)))};
    }

    inline bool wants_angle(DataKind kind) { return kind == DataKind::Angle || kind == DataKind::Combined; }

    inline bool wants_tactile(DataKind kind) { return kind == DataKind::Tactile || kind == DataKind::Combined; }
} // namespace l6_benchmark
