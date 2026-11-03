#include "linkerbot/hand/l6/config.hpp"
#include <chrono>
#include <cstdint>
#include <linkerbot/hand/l6/config_manager.hpp>
#include <linkerbot/hand/l6/frame.hpp>
#include <linkerbot/hand/l6/protocol_backend.hpp>
#include <stdexcept>

namespace linkerbot::hand::l6 {

    ConfigManager::ConfigManager(uint32_t id, can::CANDispatcher &dispatcher)
        : ConfigManager(id, detail::borrow_runtime(dispatcher, id)) {}
    ConfigManager::ConfigManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(id), ids_(std::move(runtime), "config.id") {}
    ConfigManager::~ConfigManager() {
        try {
            stop();
            // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
            // NOLINTNEXTLINE(bugprone-empty-catch)
        } catch (...) {
        }
        ids_.drain_calls();
    }
    void ConfigManager::start() { ids_.start(); }
    void ConfigManager::stop() { ids_.stop(); }

    CanIds ConfigManager::set_id(uint8_t id, std::chrono::milliseconds timeout_ms) {
        const auto call = ids_.enter();
        return set_id_with_subcmd(kSetTxIdSubcmd, id, timeout_ms);
    }

    CanIds ConfigManager::set_transmit_id(uint8_t id, std::chrono::milliseconds timeout_ms) {
        const auto call = ids_.enter();
        return set_id_with_subcmd(kSetTxIdSubcmd, id, timeout_ms);
    }

    CanIds ConfigManager::set_receive_id(uint8_t id, std::chrono::milliseconds timeout_ms) {
        const auto call = ids_.enter();
        return set_id_with_subcmd(kSetRxIdSubcmd, id, timeout_ms);
    }

    void ConfigManager::set_reply_enabled(bool enabled) {
        const auto call = ids_.enter();
        send_filled(kReplyCmd, enabled ? uint8_t{0x01} : kReplyCmd, 7);
        std::function<void(bool)> listener;
        {
            const std::scoped_lock lock(reply_mutex_);
            reply_enabled_ = enabled;
            listener = reply_listener_;
        }
        if (listener) {
            listener(enabled);
        }
    }

    std::optional<bool> ConfigManager::reply_enabled() const {
        const std::scoped_lock lock(reply_mutex_);
        return reply_enabled_;
    }

    void ConfigManager::set_reply_change_listener(std::function<void(bool)> listener) {
        const std::scoped_lock lock(reply_mutex_);
        reply_listener_ = std::move(listener);
    }

    void ConfigManager::restore_factory() {
        const auto call = ids_.enter();
        send_filled(kFactoryCmd, kFactoryCmd, 7);
    }

    void ConfigManager::save() {
        const auto call = ids_.enter();
        send_filled(kSaveCmd, kSaveCmd, 7);
    }

    CanIds ConfigManager::set_id_with_subcmd(uint8_t subcmd, uint8_t id, std::chrono::milliseconds timeout_ms) {
        const auto call = ids_.enter();
        const auto deadline = runtime::deadline_after(timeout_ms);
        return ids_.get(protocol::set_id(arbitration_id_, subcmd, id), deadline);
    }

    void ConfigManager::send_filled(uint8_t cmd, uint8_t last_byte, uint8_t dlc) {
        const auto call = ids_.enter();
        ids_.command(protocol::filled_command(arbitration_id_, cmd, last_byte, dlc));
    }

} // namespace linkerbot::hand::l6
