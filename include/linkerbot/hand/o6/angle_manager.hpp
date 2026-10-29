#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/angle_mapping.hpp>
#include <linkerbot/hand/o6/angle.hpp>
#include <linkerbot/hand/o6/protocol_backend.hpp>
#include <linkerbot/hand/runtime/channel.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/hand/runtime/event_binding.hpp>
#include <linkerbot/relay.hpp>

namespace linkerbot::hand::o6 {
    class AngleManager {
        friend struct ManagerTestAccess;
        friend class O6;

    public:
        AngleManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher,
                     const std::optional<std::filesystem::path> &angle_mapping_path = {},
                     std::string_view side = "unknown", std::string_view interface_name = "unknown");
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

        // Protocol-native actuator bytes. Values bypass the configurable LUT.
        void set_angles(const std::array<uint8_t, Angle::kJointCount> &angles);
        void set_angles_percent(const Angle &angles);
        void set_angles_percent(const std::array<float, Angle::kJointCount> &values);

        [[nodiscard]] AngleMapping get_angle_mapping() const;
        [[nodiscard]] AngleMapping get_default_angle_mapping() const;
        void set_angle_mapping(const AngleMapping &mapping);
        void reset_angle_mapping();

        [[nodiscard]] AngleData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<AngleData> get_snapshot() const;

        [[nodiscard]] AngleData set_angles_blocking(const std::array<uint8_t, Angle::kJointCount> &raw_angles,
                                                    std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                        100});

        struct StatsSnapshot {
            uint64_t query_count = 0;
            uint64_t command_feedback_count = 0;
        };
        [[nodiscard]] StatsSnapshot stats() const;

    private:
        static constexpr uint8_t kControlCmd = 0x01;
        static constexpr uint8_t kSenseCmd = 0x01;

        void send_mapped_angles(const std::array<uint8_t, Angle::kJointCount> &raw_angles);
        void send_sense_request();
        void set_event_sink(std::function<void(const AngleData &)> sink);

        void cancel_pending();
        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        AngleMappingManager mapping_;
        runtime::Channel<AngleData> channel_;
        std::atomic<uint64_t> query_count_{0};
        std::atomic<uint64_t> command_feedback_count_{0};
    };
} // namespace linkerbot::hand::o6
