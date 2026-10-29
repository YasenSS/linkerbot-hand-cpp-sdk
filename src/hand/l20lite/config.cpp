#include <linkerbot/hand/l20lite/config_manager.hpp>
namespace linkerbot::hand::l20lite {
    ConfigManager::ConfigManager(uint32_t id, can::CANDispatcher &dispatcher)
        : ConfigManager(id,
                        runtime::DeviceRuntime::borrow(dispatcher, "l20lite/" + std::to_string(id), {{id, false}})) {}
    ConfigManager::ConfigManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), runtime_(std::move(runtime)), channel_(runtime_, "config") {}
    ConfigManager::~ConfigManager() = default;
    void ConfigManager::start() { channel_.start(); }
    void ConfigManager::stop() { channel_.stop(); }
    CanIds ConfigManager::set_id(uint8_t id, std::chrono::milliseconds timeout_ms) {
        const auto deadline = runtime::deadline_after(timeout_ms);
        return channel_.get(protocol::read_config_set_id(arbitration_id_, 1, id), deadline);
    }
    CanIds ConfigManager::set_device_id(uint8_t id, std::chrono::milliseconds timeout_ms) {
        const auto deadline = runtime::deadline_after(timeout_ms);
        return channel_.get(protocol::read_config_set_id(arbitration_id_, 2, id), deadline);
    }
    CanIds ConfigManager::set_transmit_id(uint8_t id, std::chrono::milliseconds timeout_ms) {
        const auto deadline = runtime::deadline_after(timeout_ms);
        return channel_.get(protocol::read_config_set_id(arbitration_id_, 3, id), deadline);
    }
    void ConfigManager::set_reply_enabled(bool enabled) {
        channel_.command(protocol::write_config_reply_enabled(arbitration_id_, enabled));
    }
    void ConfigManager::restore_factory() { channel_.command(protocol::write_config_restore_factory(arbitration_id_)); }
    void ConfigManager::save() { channel_.command(protocol::write_config_save(arbitration_id_)); }
}
