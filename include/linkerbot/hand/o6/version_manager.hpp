#pragma once

#include <linkerbot/hand/identity.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/o6/protocol_backend.hpp>
#include <linkerbot/hand/o6/version.hpp>
#include <linkerbot/hand/runtime/channel.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/relay.hpp>

namespace linkerbot::hand::o6 {

    class VersionManager {
        friend class O6;
        friend struct ManagerTestAccess;

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
        void cancel_pending() { channel_.cancel(); }
        std::string get_serial_number_blocking(runtime::Deadline deadline, runtime::CancellationToken cancellation);
        Version get_pcb_version_blocking(runtime::Deadline deadline, runtime::CancellationToken cancellation);
        Version get_firmware_version_blocking(runtime::Deadline deadline, runtime::CancellationToken cancellation);
        Version get_mechanical_version_blocking(runtime::Deadline deadline, runtime::CancellationToken cancellation);

        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        runtime::Channel<DeviceInfo> channel_;
    };

} // namespace linkerbot::hand::o6
