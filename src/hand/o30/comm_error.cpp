#include "linkerbot/hand/o30/comm_error_manager.hpp"

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    CommErrorManager::CommErrorManager(Client &client) : client_(&client) {}

    uint8_t CommErrorManager::get_latest_index(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        const auto payload = client_->read(protocol::kMiCommError, 0x00, 1, false, timeout_ms);
        if (payload.empty()) {
            throw protocol::ProtocolError("comm-error index reply is empty");
        }
        return payload[0];
    }

    CommErrorData CommErrorManager::get_history(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        CommErrorData data{};
        data.latest_index = get_latest_index(timeout_ms);
        const auto history =
            client_->read(protocol::kMiCommError, 0x01, protocol::kErrorHistoryBytes, false, timeout_ms);
        if (history.size() != protocol::kErrorHistoryBytes && history.size() != protocol::kErrorHistoryBytes - 1) {
            throw protocol::ProtocolError("comm-error history must contain 15 or 16 bytes");
        }
        data.history = history;
        data.timestamp = client_->received_at();
        return data;
    }

} // namespace linkerbot::hand::o30
