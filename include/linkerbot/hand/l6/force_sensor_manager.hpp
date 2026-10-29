#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>
#include <linkerbot/hand/l6/force_sensor.hpp>
#include <linkerbot/hand/l6/runtime.hpp>

namespace linkerbot::hand::l6 {

    class SingleForceSensorManager {
        friend class L6;
        friend struct L6RuntimeTestAccess;
        friend class ForceSensorManager;

    public:
        SingleForceSensorManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher, uint8_t command_prefix,
                                 std::shared_ptr<std::timed_mutex> transaction_gate = {});
        SingleForceSensorManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime,
                                 uint8_t command_prefix);
        ~SingleForceSensorManager();

        SingleForceSensorManager(const SingleForceSensorManager &) = delete;
        SingleForceSensorManager &operator=(const SingleForceSensorManager &) = delete;
        SingleForceSensorManager(SingleForceSensorManager &&) = delete;
        SingleForceSensorManager &operator=(SingleForceSensorManager &&) = delete;

        void start();
        void stop();

        [[nodiscard]] ForceSensorData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                       1000});
        [[nodiscard]] std::optional<ForceSensorData> get_snapshot() const;
        [[nodiscard]] size_t pending_count() const;
        [[nodiscard]] uint64_t in_flight_count() const;

    private:
        ForceSensorData get_until(std::chrono::steady_clock::time_point deadline,
                                  runtime::Sample *provenance = nullptr);
        static constexpr uint8_t kRequestByte = 0xC6;
        static constexpr auto kLateResponseGuard = std::chrono::milliseconds{5};

        void set_event_sink(std::function<void(const ForceSensorData &)> sink);
        void attach_stats(RuntimeStats *stats);
        void set_late_response_guard(std::chrono::milliseconds guard);
        void cancel_waiters();
        void resume_transactions();

        uint32_t arbitration_id_;
        uint8_t command_prefix_;
        detail::Channel<ForceSensorData> channel_;
        std::shared_ptr<std::timed_mutex> external_gate_;
        std::atomic<int64_t> late_guard_ms_{5};
        std::atomic<RuntimeStats *> stats_{nullptr};
    };

    class ForceSensorManager {
        friend class L6;
        friend struct L6RuntimeTestAccess;

    public:
        ForceSensorManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher,
                           const std::shared_ptr<std::timed_mutex> &transaction_gate = {});
        ForceSensorManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~ForceSensorManager();

        ForceSensorManager(const ForceSensorManager &) = delete;
        ForceSensorManager &operator=(const ForceSensorManager &) = delete;
        ForceSensorManager(ForceSensorManager &&) = delete;
        ForceSensorManager &operator=(ForceSensorManager &&) = delete;

        void start();
        void stop();

        [[nodiscard]] AllFingersData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                      1000});
        [[nodiscard]] std::optional<AllFingersData> get_snapshot() const;
        [[nodiscard]] SingleForceSensorManager &get_finger(const std::string &name);
        [[nodiscard]] size_t pending_count() const;
        [[nodiscard]] uint64_t in_flight_count() const;

    private:
        using BeforeFinger = std::function<void(size_t, std::chrono::steady_clock::time_point)>;
        AllFingersData get_staged(std::chrono::steady_clock::time_point deadline, const BeforeFinger &before_finger);
        void set_event_sink(std::function<void(const AllFingersData &)> sink);
        void attach_stats(RuntimeStats *stats);
        void set_late_response_guard(std::chrono::milliseconds guard);
        void cancel_waiters();
        void resume_transactions();

        std::unordered_map<std::string, std::unique_ptr<SingleForceSensorManager>> fingers_;
        detail::Channel<AllFingersData> channel_;
        RuntimeStats *stats_ = nullptr;
    };

} // namespace linkerbot::hand::l6
