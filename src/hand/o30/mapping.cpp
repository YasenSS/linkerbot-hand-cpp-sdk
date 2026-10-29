#include "linkerbot/hand/o30/mapping_manager.hpp"

#include <vector>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    MappingManager::MappingManager(Client &client) : client_(&client) {}

    std::array<uint8_t, kSlotCount> MappingManager::get_map(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        return protocol::as_u8_slots(
            client_->read(protocol::kMiMapping, 0, protocol::kU8ObjectBytes, false, timeout_ms));
    }

    JointMapping MappingManager::get_length_and_enable(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        const auto payload = client_->read(protocol::kMiMapping, 0x24, 2, false, timeout_ms);
        if (payload.size() < 2) {
            throw protocol::ProtocolError("mapping length/enable reply too short");
        }
        JointMapping mapping{};
        mapping.length = payload[0];
        mapping.enabled = payload[1];
        mapping.timestamp = client_->received_at();
        return mapping;
    }

    void MappingManager::set_map(const std::array<uint8_t, kSlotCount> &map, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        static_cast<void>(
            client_->write(protocol::kMiMapping, 0, std::vector<uint8_t>(map.begin(), map.end()), timeout_ms));
    }

    void MappingManager::set_length_and_enable(uint8_t length, uint8_t enabled, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        static_cast<void>(client_->write(protocol::kMiMapping, 0x24, {length, enabled}, timeout_ms));
    }

} // namespace linkerbot::hand::o30
