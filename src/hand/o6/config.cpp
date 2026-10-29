#include "linkerbot/hand/o6/config.hpp"
#include <cstdint>
#include <linkerbot/hand/o6/config_manager.hpp>
#include <memory>
#include <string>
#include <utility>

namespace linkerbot::hand::o6 {

    ConfigManager::ConfigManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher)
        : ConfigManager(arbitration_id,
                        runtime::DeviceRuntime::borrow(dispatcher, "o6/" + std::to_string(arbitration_id),
                                                       {{arbitration_id, false}})) {}

    ConfigManager::ConfigManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          channel_(runtime_, "config") {}

    void ConfigManager::set_id(uint8_t id) { channel_.command(protocol_.make_command("config_set_id", id)); }

    void ConfigManager::set_device_id(uint8_t id) {
        channel_.command(protocol_.make_command("config_set_device_id", id));
    }

    void ConfigManager::set_transmit_id(uint8_t id) {
        channel_.command(protocol_.make_command("config_set_transmit_id", id));
    }

    void ConfigManager::restore_factory() { channel_.command(protocol_.make_command("config_restore_factory")); }

    void ConfigManager::save() { channel_.command(protocol_.make_command("config_save")); }

} // namespace linkerbot::hand::o6
