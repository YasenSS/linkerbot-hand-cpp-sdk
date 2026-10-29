#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/angle_mapping.hpp>
#include <linkerbot/hand/l20lite/angle.hpp>
#include <linkerbot/hand/l20lite/detail/channel.hpp>
#include <linkerbot/hand/l20lite/protocol_backend.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>

namespace linkerbot::hand::l20lite {
    class AngleManager {
        friend class L20lite;
        friend struct ManagerTestAccess;

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

    private:
        void cancel_pending() { channel_.cancel(); }
        void send_mapped_angles(const std::array<uint8_t, Angle::kJointCount> &raw_angles);
        void send_sense_request();
        void set_event_sink(std::function<void(const AngleData &)> sink);

        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        AngleMappingManager mapping_;
        detail::Channel<AngleData> channel_;
    };
} // namespace linkerbot::hand::l20lite
