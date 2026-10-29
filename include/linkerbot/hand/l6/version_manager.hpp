#pragma once

#include <linkerbot/hand/identity.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>
#include <linkerbot/hand/l6/version.hpp>

namespace linkerbot::hand::l6 {

    class VersionManager {
        friend class L6;

    public:
        VersionManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        VersionManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~VersionManager();

        VersionManager(const VersionManager &) = delete;
        VersionManager &operator=(const VersionManager &) = delete;
        VersionManager(VersionManager &&) = delete;
        VersionManager &operator=(VersionManager &&) = delete;

        void start();
        void stop();

        [[nodiscard]] DeviceInfo get_device_info(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                     1000});

        // Opt-in factory model/side validation, using this manager's CAN endpoint.
        [[nodiscard]] DeviceInfo
        get_verified_device_info(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{1000});

    private:
        static constexpr uint8_t kSnCmd = 0xC0;
        static constexpr uint8_t kPcbVersionCmd = 0xC1;
        static constexpr uint8_t kFirmwareVersionCmd = 0xC2;
        static constexpr uint8_t kMechanicalVersionCmd = 0xC4;
        std::string get_serial_number_blocking(std::chrono::milliseconds timeout_ms);
        Version get_pcb_version_blocking(std::chrono::milliseconds timeout_ms);
        Version get_firmware_version_blocking(std::chrono::milliseconds timeout_ms);
        Version get_mechanical_version_blocking(std::chrono::milliseconds timeout_ms);

        uint32_t arbitration_id_;
        detail::Channel<std::string> serial_;
        detail::Channel<Version> pcb_, firmware_, mechanical_;
    };

} // namespace linkerbot::hand::l6
