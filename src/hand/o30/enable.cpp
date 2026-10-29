#include "linkerbot/hand/o30/enable_manager.hpp"

#include "u8_io.hpp"

#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    EnableManager::EnableManager(Client &client) : client_(&client), relay_(client.runtime(), "enable/relay_") {}

    void EnableManager::set_masks(const std::array<uint8_t, kJointCount> &, std::chrono::milliseconds) {
        throw UnsupportedOperationError(
            "O30i enable control is unsupported: 0.0.3 masks are ineffective; 0.0.4 has no enable command");
    }

    EnableData EnableManager::get_blocking(std::chrono::milliseconds, ValueKind) {
        throw UnsupportedOperationError(
            "O30i has no reliable enable status; valid-joint masks are not enable feedback");
    }

    std::optional<EnableData> EnableManager::get_snapshot() const { return relay_.snapshot(); }

} // namespace linkerbot::hand::o30
