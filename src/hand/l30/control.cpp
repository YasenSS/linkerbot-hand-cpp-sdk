#include "linkerbot/hand/l30/control_manager.hpp"
#include <linkerbot/hand/errors.hpp>
#include <stdexcept>

namespace linkerbot::hand::l30 {

    namespace {
        void send_control_write(Client &client, uint8_t subcmd, std::chrono::milliseconds timeout_ms) {
            if (timeout_ms.count() <= 0) {
                throw ValidationError("timeout_ms must be positive");
            }
            const auto response =
                client.request_ack(protocol::kParentControl, subcmd, protocol::empty_request_payload(),
                                   protocol::kEmptyRequestBytes, timeout_ms);
            protocol::check_response_status(response.data(), response.size());
        }
    } // namespace

    ControlManager::ControlManager(Client &client) : client_(&client) {}

    void ControlManager::enable(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        send_control_write(*client_, protocol::kSubEnable, timeout_ms);
    }

    void ControlManager::disable(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        send_control_write(*client_, protocol::kSubDisable, timeout_ms);
    }

} // namespace linkerbot::hand::l30
