#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <optional>
#include <vector>

namespace linkerbot::hand::l6 {
    enum class AngleFeedbackPolicy { QueryOnly, CommandFeedbackOnly, Adaptive };

    struct InterstageOptions {
        // Zero preserves the old uninterrupted five-finger scheduling.
        unsigned max_angle_transactions = 4;
        bool query_lookahead = false;
        std::chrono::microseconds finger_estimate{2500};
        std::chrono::microseconds timing_margin{100};
        // Admission estimate used to reserve space before the next finger.
        std::chrono::microseconds angle_budget{2000};
        // Hard deadline for an admitted interstage angle transaction.
        std::chrono::microseconds request_timeout{5000};
    };

    // Public aggregate layout and positional initialization are compatibility contracts.
    // NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
    struct L6PollingPlan {
        bool angle_enabled = false;
        std::chrono::microseconds angle_period{100000};

        bool tactile_enabled = false;
        std::chrono::microseconds tactile_period{100000};

        bool current_limit_enabled = false;
        std::chrono::microseconds current_limit_period{100000};
        bool torque_enabled = false;
        std::chrono::microseconds torque_period{100000};
        bool temperature_enabled = false;
        std::chrono::microseconds temperature_period{100000};
        bool current_enabled = false;
        std::chrono::microseconds current_period{100000};
        bool fault_enabled = false;
        std::chrono::microseconds fault_period{100000};

        AngleFeedbackPolicy angle_policy = AngleFeedbackPolicy::QueryOnly;

        std::chrono::milliseconds request_timeout{100};
        std::chrono::milliseconds tactile_timeout{1000};
        std::chrono::milliseconds late_response_guard{5};
        InterstageOptions interstage{};
        // Optional absolute cutoff: no new polling transaction starts at/after it.
        std::optional<std::chrono::steady_clock::time_point> polling_until;
    };

    struct RuntimeStatsSnapshot {
        uint64_t scheduled_count = 0;
        uint64_t sent_count = 0;
        uint64_t completed_count = 0;
        uint64_t timeout_count = 0;
        uint64_t protocol_error_count = 0;
        uint64_t late_response_count = 0;
        uint64_t duplicate_count = 0;
        uint64_t conflicting_duplicate_count = 0;
        uint64_t skipped_deadline_count = 0;
        uint64_t event_drop_count = 0;
        uint64_t pending_count = 0;
        uint64_t in_flight_count = 0;
        uint64_t query_angle_count = 0;
        uint64_t command_feedback_angle_count = 0;
        uint64_t unknown_origin_count = 0;

        uint64_t angle_scheduled_count = 0;
        uint64_t tactile_scheduled_count = 0;
        uint64_t angle_sent_count = 0;
        uint64_t tactile_sent_count = 0;
        uint64_t angle_completed_count = 0;
        uint64_t tactile_completed_count = 0;
        uint64_t angle_timeout_count = 0;
        uint64_t tactile_timeout_count = 0;
        uint64_t interstage_angle_timeout_count = 0;
        std::array<uint64_t, 5> tactile_finger_timeout_count{};

        uint64_t angle_skipped_deadline_count = 0;
        uint64_t tactile_skipped_deadline_count = 0;
        uint64_t interstage_angle_count = 0;
        uint64_t interstage_wait_count = 0;
        uint64_t interstage_budget_rejection_count = 0;
        uint64_t tactile_service_overrun_count = 0;

        struct Distribution {
            uint64_t count = 0;
            double min_us = 0.0;
            double mean_us = 0.0;
            double p50_us = 0.0;
            double p95_us = 0.0;
            double p99_us = 0.0;
            double max_us = 0.0;
        };

        Distribution angle_latency;
        Distribution tactile_latency;
        Distribution deadline_jitter;
        Distribution angle_deadline_latency;
        Distribution interstage_wait;
        Distribution interstage_work;
        std::array<Distribution, 5> finger_latency{};
    };

    class RuntimeStats {
    public:
        void inc_scheduled() { scheduled_count_.fetch_add(1); }
        void inc_sent() { sent_count_.fetch_add(1); }
        void inc_completed() { completed_count_.fetch_add(1); }
        void inc_timeout() { timeout_count_.fetch_add(1); }
        void inc_protocol_error() { protocol_error_count_.fetch_add(1); }
        void inc_late_response() { late_response_count_.fetch_add(1); }
        void inc_duplicate() { duplicate_count_.fetch_add(1); }
        void inc_conflicting_duplicate() { conflicting_duplicate_count_.fetch_add(1); }
        void add_skipped_deadlines(uint64_t n) { skipped_deadline_count_.fetch_add(n); }
        void inc_query_angle() { query_angle_count_.fetch_add(1); }
        void inc_command_feedback_angle() { command_feedback_angle_count_.fetch_add(1); }
        void inc_unknown_origin() { unknown_origin_count_.fetch_add(1); }
        void inc_angle_scheduled() { angle_scheduled_count_.fetch_add(1); }
        void inc_tactile_scheduled() { tactile_scheduled_count_.fetch_add(1); }
        void inc_angle_sent() { angle_sent_count_.fetch_add(1); }
        void inc_tactile_sent() { tactile_sent_count_.fetch_add(1); }
        void inc_angle_completed() { angle_completed_count_.fetch_add(1); }
        void inc_tactile_completed() { tactile_completed_count_.fetch_add(1); }
        void inc_angle_timeout() { angle_timeout_count_.fetch_add(1); }
        void inc_tactile_timeout(size_t finger_index) {
            tactile_timeout_count_.fetch_add(1);
            if (finger_index < tactile_finger_timeout_count_.size()) {
                tactile_finger_timeout_count_[finger_index].fetch_add(1);
            }
        }
        void inc_interstage_angle_timeout() { interstage_angle_timeout_count_.fetch_add(1); }
        void record_angle_latency(std::chrono::microseconds value) { record(angle_latencies_us_, value); }
        void record_tactile_latency(std::chrono::microseconds value) { record(tactile_latencies_us_, value); }
        void record_deadline_jitter(std::chrono::microseconds value) { record(deadline_jitters_us_, value); }

        void add_angle_skipped_deadline_count(uint64_t n = 1) { angle_skipped_deadline_count_.fetch_add(n); }
        void add_tactile_skipped_deadline_count(uint64_t n = 1) { tactile_skipped_deadline_count_.fetch_add(n); }
        void add_interstage_angle_count(uint64_t n = 1) { interstage_angle_count_.fetch_add(n); }
        void add_interstage_wait_count(uint64_t n = 1) { interstage_wait_count_.fetch_add(n); }
        void add_interstage_budget_rejection_count(uint64_t n = 1) { interstage_budget_rejection_count_.fetch_add(n); }
        void add_tactile_service_overrun_count(uint64_t n = 1) { tactile_service_overrun_count_.fetch_add(n); }
        void record_angle_deadline_latency(std::chrono::microseconds value) {
            record(angle_deadline_latency_us_, value);
        }
        void record_interstage_wait(std::chrono::microseconds value) { record(interstage_wait_us_, value); }
        void record_interstage_work(std::chrono::microseconds value) { record(interstage_work_us_, value); }
        void record_finger_latency(size_t index, std::chrono::microseconds value) {
            record(finger_latencies_us_.at(index), value);
        }

        RuntimeStatsSnapshot snapshot() const {
            RuntimeStatsSnapshot out;
            out.angle_skipped_deadline_count = angle_skipped_deadline_count_.load();
            out.tactile_skipped_deadline_count = tactile_skipped_deadline_count_.load();
            out.interstage_angle_count = interstage_angle_count_.load();
            out.interstage_wait_count = interstage_wait_count_.load();
            out.interstage_budget_rejection_count = interstage_budget_rejection_count_.load();
            out.tactile_service_overrun_count = tactile_service_overrun_count_.load();
            out.scheduled_count = scheduled_count_.load();
            out.sent_count = sent_count_.load();
            out.completed_count = completed_count_.load();
            out.timeout_count = timeout_count_.load();
            out.protocol_error_count = protocol_error_count_.load();
            out.late_response_count = late_response_count_.load();
            out.duplicate_count = duplicate_count_.load();
            out.conflicting_duplicate_count = conflicting_duplicate_count_.load();
            out.skipped_deadline_count = skipped_deadline_count_.load();
            out.query_angle_count = query_angle_count_.load();
            out.command_feedback_angle_count = command_feedback_angle_count_.load();
            out.unknown_origin_count = unknown_origin_count_.load();
            out.angle_scheduled_count = angle_scheduled_count_.load();
            out.tactile_scheduled_count = tactile_scheduled_count_.load();
            out.angle_sent_count = angle_sent_count_.load();
            out.tactile_sent_count = tactile_sent_count_.load();
            out.angle_completed_count = angle_completed_count_.load();
            out.tactile_completed_count = tactile_completed_count_.load();
            out.angle_timeout_count = angle_timeout_count_.load();
            out.tactile_timeout_count = tactile_timeout_count_.load();
            out.interstage_angle_timeout_count = interstage_angle_timeout_count_.load();
            for (size_t i = 0; i < tactile_finger_timeout_count_.size(); ++i) {
                out.tactile_finger_timeout_count[i] = tactile_finger_timeout_count_[i].load();
            }
            {
                const std::scoped_lock lock(distribution_mutex_);
                out.angle_latency = summarize(angle_latencies_us_);
                out.tactile_latency = summarize(tactile_latencies_us_);
                out.deadline_jitter = summarize(deadline_jitters_us_);
                out.angle_deadline_latency = summarize(angle_deadline_latency_us_);
                out.interstage_wait = summarize(interstage_wait_us_);
                out.interstage_work = summarize(interstage_work_us_);
                for (size_t i = 0; i < 5; ++i) {
                    out.finger_latency[i] = summarize(finger_latencies_us_[i]);
                }
            }
            return out;
        }

        void reset() {
            angle_skipped_deadline_count_.store(0);
            tactile_skipped_deadline_count_.store(0);
            interstage_angle_count_.store(0);
            interstage_wait_count_.store(0);
            interstage_budget_rejection_count_.store(0);
            tactile_service_overrun_count_.store(0);
            scheduled_count_.store(0);
            sent_count_.store(0);
            completed_count_.store(0);
            timeout_count_.store(0);
            protocol_error_count_.store(0);
            late_response_count_.store(0);
            duplicate_count_.store(0);
            conflicting_duplicate_count_.store(0);
            skipped_deadline_count_.store(0);
            query_angle_count_.store(0);
            command_feedback_angle_count_.store(0);
            unknown_origin_count_.store(0);
            angle_scheduled_count_.store(0);
            tactile_scheduled_count_.store(0);
            angle_sent_count_.store(0);
            tactile_sent_count_.store(0);
            angle_completed_count_.store(0);
            tactile_completed_count_.store(0);
            angle_timeout_count_.store(0);
            tactile_timeout_count_.store(0);
            interstage_angle_timeout_count_.store(0);
            for (auto &count : tactile_finger_timeout_count_) {
                count.store(0);
            }
            {
                const std::scoped_lock lock(distribution_mutex_);
                angle_latencies_us_.clear();
                tactile_latencies_us_.clear();
                deadline_jitters_us_.clear();
                angle_deadline_latency_us_.clear();
                interstage_wait_us_.clear();
                interstage_work_us_.clear();
                for (auto &values : finger_latencies_us_) {
                    values.clear();
                }
            }
        }

    private:
        static RuntimeStatsSnapshot::Distribution summarize(const std::vector<double> &values) {
            RuntimeStatsSnapshot::Distribution out;
            if (values.empty()) {
                return out;
            }
            auto sorted = values;
            std::sort(sorted.begin(), sorted.end());
            const auto at_percentile = [&sorted](double percentile) {
                const auto index = static_cast<size_t>(percentile * static_cast<double>(sorted.size() - 1));
                return sorted[index];
            };
            out.count = sorted.size();
            out.min_us = sorted.front();
            out.mean_us = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
            out.p50_us = at_percentile(0.50);
            out.p95_us = at_percentile(0.95);
            out.p99_us = at_percentile(0.99);
            out.max_us = sorted.back();
            return out;
        }

        void record(std::vector<double> &values, std::chrono::microseconds value) {
            const std::scoped_lock lock(distribution_mutex_);
            values.push_back(static_cast<double>(value.count()));
        }

        std::atomic<uint64_t> scheduled_count_{0};
        std::atomic<uint64_t> sent_count_{0};
        std::atomic<uint64_t> completed_count_{0};
        std::atomic<uint64_t> timeout_count_{0};
        std::atomic<uint64_t> protocol_error_count_{0};
        std::atomic<uint64_t> late_response_count_{0};
        std::atomic<uint64_t> duplicate_count_{0};
        std::atomic<uint64_t> conflicting_duplicate_count_{0};
        std::atomic<uint64_t> skipped_deadline_count_{0};
        std::atomic<uint64_t> query_angle_count_{0};
        std::atomic<uint64_t> command_feedback_angle_count_{0};
        std::atomic<uint64_t> unknown_origin_count_{0};
        std::atomic<uint64_t> angle_scheduled_count_{0};
        std::atomic<uint64_t> tactile_scheduled_count_{0};
        std::atomic<uint64_t> angle_sent_count_{0};
        std::atomic<uint64_t> tactile_sent_count_{0};
        std::atomic<uint64_t> angle_completed_count_{0};
        std::atomic<uint64_t> tactile_completed_count_{0};
        std::atomic<uint64_t> angle_timeout_count_{0};
        std::atomic<uint64_t> tactile_timeout_count_{0};
        std::atomic<uint64_t> interstage_angle_timeout_count_{0};
        std::array<std::atomic<uint64_t>, 5> tactile_finger_timeout_count_{};
        std::atomic<uint64_t> angle_skipped_deadline_count_{0};
        std::atomic<uint64_t> tactile_skipped_deadline_count_{0};
        std::atomic<uint64_t> interstage_angle_count_{0};
        std::atomic<uint64_t> interstage_wait_count_{0};
        std::atomic<uint64_t> interstage_budget_rejection_count_{0};
        std::atomic<uint64_t> tactile_service_overrun_count_{0};
        std::vector<double> angle_deadline_latency_us_;
        std::vector<double> interstage_wait_us_;
        std::vector<double> interstage_work_us_;
        std::array<std::vector<double>, 5> finger_latencies_us_;
        mutable std::mutex distribution_mutex_;
        std::vector<double> angle_latencies_us_;
        std::vector<double> tactile_latencies_us_;
        std::vector<double> deadline_jitters_us_;
    };
} // namespace linkerbot::hand::l6
