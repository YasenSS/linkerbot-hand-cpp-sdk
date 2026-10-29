#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/angle_mapping.hpp>
#include <linkerbot/hand/l6/angle.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>
#include <linkerbot/hand/l6/runtime.hpp>

namespace linkerbot::hand::l6 {
    class AngleManager {
        friend class L6;
        friend struct L6RuntimeTestAccess;

    public:
        AngleManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher,
                     const std::optional<std::filesystem::path> &angle_mapping_path = {},
                     std::string_view side = "unknown", std::string_view interface_name = "unknown",
                     std::shared_ptr<std::timed_mutex> transaction_gate = {});
        AngleManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime,
                     const std::optional<std::filesystem::path> &angle_mapping_path = {},
                     std::string_view side = "unknown", std::string_view interface_name = "unknown");
        ~AngleManager();

        AngleManager(const AngleManager &) = delete;
        AngleManager &operator=(const AngleManager &) = delete;
        AngleManager(AngleManager &&) = delete;
        AngleManager &operator=(AngleManager &&) = delete;

        void start();
        void stop();

        // Protocol-native actuator bytes. Values are sent unchanged and do not
        // pass through the configurable logical-angle mapping.
        void set_angles(const std::array<uint8_t, Angle::kJointCount> &angles);
        void set_angles_percent(const Angle &angles);
        void set_angles_percent(const std::array<float, Angle::kJointCount> &values);

        AngleData set_angles_blocking(const std::array<uint8_t, Angle::kJointCount> &raw_angles,
                                      std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        AngleData set_angles_percent_blocking(const Angle &angles,
                                              std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] AngleMapping get_angle_mapping() const;
        [[nodiscard]] AngleMapping get_default_angle_mapping() const;
        void set_angle_mapping(const AngleMapping &mapping);
        void reset_angle_mapping();

        [[nodiscard]] AngleData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<AngleData> get_snapshot() const;

        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> last_command_feedback_time() const;
        [[nodiscard]] size_t pending_count() const;
        [[nodiscard]] uint64_t in_flight_count() const;

    private:
        static constexpr uint8_t kControlCmd = 0x01;
        static constexpr uint8_t kSenseCmd = 0x01;

        void set_angles_impl(const std::array<uint8_t, Angle::kJointCount> &raw_angles, bool apply_mapping);
        AngleData set_angles_blocking_impl(const std::array<uint8_t, Angle::kJointCount> &raw_angles,
                                           std::chrono::milliseconds timeout_ms, bool apply_mapping);
        void send_sense_request();
        void set_event_sink(std::function<void(const AngleData &)> sink);
        void attach_stats(RuntimeStats *stats);
        void set_reply_enabled(std::optional<bool> enabled);
        void set_late_response_guard(std::chrono::milliseconds guard);
        void set_command_timeout(std::chrono::milliseconds timeout);
        void set_command_feedback_listener(std::function<void()> listener);
        void cancel_waiters();
        void resume_transactions();

        AngleData get_until(runtime::Deadline deadline);
        runtime::Sample transact(runtime::TransactionPlan plan, runtime::Deadline deadline);
        std::array<uint8_t, Angle::kJointCount> mapped(const std::array<uint8_t, Angle::kJointCount> &raw,
                                                       bool apply) const;

        uint32_t arbitration_id_;
        AngleMappingManager mapping_;
        detail::Channel<AngleData> channel_;
        std::shared_ptr<std::timed_mutex> external_gate_;
        mutable std::mutex tx_mutex_;
        // business options and feedback timestamp only
        // 保护共享配置和状态
        std::chrono::milliseconds late_response_guard_{5};
        // 迟到响应的保护时间，默认 5 毫秒，用于避免前一次请求迟到的回复干扰后续请求。
        std::chrono::milliseconds command_timeout_{100};
        // 普通设置角度命令的超时时间，默认 100 毫秒。
        std::optional<bool> reply_enabled_;
        // 保存设备回复开关的配置
        std::optional<std::chrono::steady_clock::time_point> last_command_feedback_;
        // 保存最近一次收到角度命令反馈的时间点
        std::function<void()> command_feedback_listener_;
        // 保存一个收到命令反馈时调用的函数
        RuntimeStats *stats_ = nullptr;
        // 向统计对象，用来记录通信次数、超时次数和延迟
    };
} // namespace linkerbot::hand::l6
