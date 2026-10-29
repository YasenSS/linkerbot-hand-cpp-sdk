#pragma once

#include "l6_frequency_benchmark_types.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l6/angle.hpp>

namespace l6_benchmark {
    struct RawAngleSample {
        std::array<uint8_t, 6> raw{};
        linkerbot::hand::l6::SampleOrigin origin = linkerbot::hand::l6::SampleOrigin::Unknown;
        std::chrono::microseconds latency{0};
        Clock::time_point timestamp{};
    };

    class RawSession {
    public:
        RawSession(std::unique_ptr<linkerbot::can::CANTransport> transport, uint32_t arbitration_id,
                   std::chrono::milliseconds late_response_guard);
        ~RawSession();

        RawSession(const RawSession &) = delete;
        RawSession &operator=(const RawSession &) = delete;

        RawAngleSample query_angle(std::chrono::milliseconds timeout);
        std::optional<RawAngleSample> command_angle(const std::array<uint8_t, 6> &target, bool expect_feedback,
                                                    std::chrono::milliseconds timeout);
        using BeforeFinger = std::function<void(size_t, Clock::time_point)>;
        std::chrono::microseconds query_all_tactile(std::chrono::milliseconds timeout,
                                                    const BeforeFinger &before_finger = {});
        void set_reply_enabled(bool enabled);

        void reset_metrics();
        void set_measurement_window(Clock::time_point begin, Clock::time_point end);
        void set_stop_predicate(std::function<bool()> stop) { stop_ = std::move(stop); }
        void check_stopped() const;
        [[nodiscard]] Metrics metrics() const;
        [[nodiscard]] linkerbot::can::TransportHealth health() const;

    private:
        struct AnglePending {
            linkerbot::hand::l6::SampleOrigin origin = linkerbot::hand::l6::SampleOrigin::Unknown;
            Clock::time_point sent_at{};
            std::optional<RawAngleSample> sample;
            std::string error;
        };

        struct FingerTiming {
            std::chrono::microseconds latency;
            Clock::time_point timestamp;
        };

        struct TactilePending {
            uint8_t opcode = 0;
            Clock::time_point sent_at{};
            std::array<std::optional<std::array<uint8_t, 6>>, 12> rows{};
            size_t count = 0;
            std::string error;
            Clock::time_point completed_at{};
        };

        RawAngleSample transact_angle(const linkerbot::can::Message &request, linkerbot::hand::l6::SampleOrigin origin,
                                      std::chrono::milliseconds timeout);
        FingerTiming query_finger(uint8_t opcode, Clock::time_point deadline);
        void wait_for_quarantine(Clock::time_point deadline);
        void on_message(const linkerbot::can::Message &message);
        void on_angle_message(const linkerbot::can::Message &message);
        void on_tactile_message(const linkerbot::can::Message &message);
        uint32_t arbitration_id_;
        std::unique_ptr<linkerbot::can::CANDispatcher> dispatcher_;
        linkerbot::can::SubscriptionId subscription_id_ = 0;
        std::chrono::milliseconds late_response_guard_;
        mutable std::mutex transaction_mutex_;
        mutable std::mutex state_mutex_;
        std::condition_variable state_cv_;
        std::optional<AnglePending> angle_pending_;
        std::optional<TactilePending> tactile_pending_;
        Clock::time_point quarantine_until_{};
        mutable std::mutex tactile_batch_mutex_;
        std::atomic<bool> tactile_batch_active_{false};
        std::function<bool()> stop_;

        mutable std::mutex metrics_mutex_;
        Metrics metrics_;
        WindowSamples window_;
        std::array<std::vector<double>, 5> finger_latencies_us_;
        std::vector<double> angle_latencies_us_;
        std::vector<double> tactile_latencies_us_;
    };

    std::unique_ptr<linkerbot::can::CANTransport> make_socketcan_transport(std::string_view interface_name,
                                                                           uint32_t arbitration_id);
} // namespace l6_benchmark
