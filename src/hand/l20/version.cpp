#include "linkerbot/hand/l20/version.hpp"
#include <chrono>
#include <cstdint>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20/version_manager.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace linkerbot::hand::l20 {

    VersionManager::VersionManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher)
        : VersionManager(arbitration_id,
                         runtime::DeviceRuntime::borrow(dispatcher, "l20/" + std::to_string(arbitration_id),
                                                        {{arbitration_id, false}})) {}

    VersionManager::VersionManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          channel_(runtime_, "version") {}

    VersionManager::~VersionManager() = default;

    void VersionManager::start() { channel_.start(); }
    void VersionManager::stop() { channel_.stop(true); }

    DeviceInfo VersionManager::get_verified_device_info(std::chrono::milliseconds timeout_ms) {
        if (arbitration_id_ != 0x28 && arbitration_id_ != 0x27) {
            throw IdentityProtocolError("unknown classic CAN identity endpoint");
        }
        auto info = get_device_info(timeout_ms);
        (void)parse_factory_identity(info.serial_number, {"L20", "LHT20", "L25", "LHL25", "LHG20"},
                                     arbitration_id_ == 0x28 ? IdentitySide::kLeft : IdentitySide::kRight);
        return info;
    }

    DeviceInfo VersionManager::get_device_info(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }

        const auto deadline = runtime::deadline_after(timeout_ms);
        const auto call = channel_.enter();
        const auto cancellation = channel_.token();
        auto op = [&] {
            try {
                return runtime_->operation("l20.version_device_info", deadline, cancellation);
            } catch (const ::linkerbot::hand::TimeoutError &error) {
                throw internal::DataRelayTimeout(error.what());
            }
        }();

        const auto fw_ver = get_firmware_version_blocking(deadline, cancellation);
        const auto mech_ver = get_mechanical_version_blocking(deadline, cancellation);
        const auto pcb_ver = get_pcb_version_blocking(deadline, cancellation);
        auto sn = get_serial_number_blocking(deadline, cancellation);

        return DeviceInfo{
            std::move(sn), pcb_ver, fw_ver, mech_ver, std::chrono::steady_clock::now(),
        };
    }

    std::string VersionManager::get_serial_number_blocking(runtime::Deadline deadline,
                                                           runtime::CancellationToken cancellation) {
        try {
            const auto sample = runtime_->execute(protocol_.make_read("version_sn"), deadline, cancellation);
            return std::any_cast<std::string>(sample.value);
        } catch (const ::linkerbot::hand::TimeoutError &error) {
            throw internal::DataRelayTimeout(error.what());
        }
    }

    Version VersionManager::get_pcb_version_blocking(runtime::Deadline deadline,
                                                     runtime::CancellationToken cancellation) {
        try {
            const auto sample = runtime_->execute(protocol_.make_read("version_pcb"), deadline, cancellation);
            return std::any_cast<Version>(sample.value);
        } catch (const ::linkerbot::hand::TimeoutError &error) {
            throw internal::DataRelayTimeout(error.what());
        }
    }

    Version VersionManager::get_firmware_version_blocking(runtime::Deadline deadline,
                                                          runtime::CancellationToken cancellation) {
        try {
            const auto sample = runtime_->execute(protocol_.make_read("version_firmware"), deadline, cancellation);
            return std::any_cast<Version>(sample.value);
        } catch (const ::linkerbot::hand::TimeoutError &error) {
            throw internal::DataRelayTimeout(error.what());
        }
    }

    Version VersionManager::get_mechanical_version_blocking(runtime::Deadline deadline,
                                                            runtime::CancellationToken cancellation) {
        try {
            const auto sample = runtime_->execute(protocol_.make_read("version_mechanical"), deadline, cancellation);
            return std::any_cast<Version>(sample.value);
        } catch (const ::linkerbot::hand::TimeoutError &error) {
            throw internal::DataRelayTimeout(error.what());
        }
    }

} // namespace linkerbot::hand::l20
