#include <linkerbot/hand/l6/protocol_backend.hpp>
#include <linkerbot/hand/l6/version_manager.hpp>
namespace linkerbot::hand::l6 {
    VersionManager::VersionManager(uint32_t id, can::CANDispatcher &dispatcher)
        : VersionManager(id, detail::borrow_runtime(dispatcher, id)) {}
    VersionManager::VersionManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> core)
        : arbitration_id_(id), serial_(core, "version.serial"), pcb_(core, "version/193"),
          firmware_(core, "version/194"), mechanical_(core, "version/196") {}
    VersionManager::~VersionManager() {
        try {
            stop();
            // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
            // NOLINTNEXTLINE(bugprone-empty-catch)
        } catch (...) {
        }
        serial_.drain_calls();
        pcb_.drain_calls();
        firmware_.drain_calls();
        mechanical_.drain_calls();
    }
    void VersionManager::start() {
        serial_.start();
        pcb_.start();
        firmware_.start();
        mechanical_.start();
    }
    void VersionManager::stop() {
        serial_.cancel();
        pcb_.cancel();
        firmware_.cancel();
        mechanical_.cancel();
        serial_.stop();
        pcb_.stop();
        firmware_.stop();
        mechanical_.stop();
    }
    DeviceInfo VersionManager::get_verified_device_info(std::chrono::milliseconds timeout_ms) {
        const auto call = serial_.enter();
        if (arbitration_id_ != 0x28 && arbitration_id_ != 0x27) {
            throw IdentityProtocolError("unknown classic CAN identity endpoint");
        }
        auto info = get_device_info(timeout_ms);
        (void)parse_factory_identity(info.serial_number, {"LHL6"},
                                     arbitration_id_ == 0x28 ? IdentitySide::kLeft : IdentitySide::kRight);
        return info;
    }

    DeviceInfo VersionManager::get_device_info(std::chrono::milliseconds timeout) {
        const auto call = serial_.enter();
        const auto deadline = runtime::deadline_after(timeout);
        auto operation = [&] {
            try {
                return serial_.core()->operation(deadline, serial_.token());
            } catch (const ::linkerbot::hand::TimeoutError &error) {
                throw internal::DataRelayTimeout(error.what());
            }
        }();
        auto fw = get_firmware_version_blocking(timeout);
        auto mechanical = get_mechanical_version_blocking(timeout);
        auto pcb = get_pcb_version_blocking(timeout);
        auto serial = get_serial_number_blocking(timeout);
        const auto received = serial_.core()->response_provenance().received_at;
        return {std::move(serial), pcb, fw, mechanical, received};
    }
    std::string VersionManager::get_serial_number_blocking(std::chrono::milliseconds timeout) {
        return serial_.get(protocol::read_serial(arbitration_id_), runtime::deadline_after(timeout));
    }
    Version VersionManager::get_pcb_version_blocking(std::chrono::milliseconds timeout) {
        return pcb_.get(protocol::read_version(arbitration_id_, kPcbVersionOpcode), runtime::deadline_after(timeout));
    }
    Version VersionManager::get_firmware_version_blocking(std::chrono::milliseconds timeout) {
        return firmware_.get(protocol::read_version(arbitration_id_, kFirmwareVersionOpcode),
                             runtime::deadline_after(timeout));
    }
    Version VersionManager::get_mechanical_version_blocking(std::chrono::milliseconds timeout) {
        return mechanical_.get(protocol::read_version(arbitration_id_, kMechanicalVersionOpcode),
                               runtime::deadline_after(timeout));
    }
} // namespace linkerbot::hand::l6
