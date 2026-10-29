#include "linkerbot/hand/o30/unit_manager.hpp"
#include <linkerbot/hand/errors.hpp>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    UnitManager::UnitManager(Client &client) : client_(&client) {}

    void UnitManager::select_target(uint8_t mi, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        protocol::require_positive_timeout(timeout_ms);
        if (mi > 0x7F) {
            throw ValidationError("unit target MI must fit in seven bits");
        }
        static_cast<void>(client_->write(protocol::kMiUnit, 0x00, {mi}, timeout_ms));
        target_mi_ = mi;
    }

    UnitInfo UnitManager::get_unit(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        protocol::require_positive_timeout(timeout_ms);
        const auto payload = client_->read(protocol::kMiUnit, 0x01, 0x11, false, timeout_ms);
        if (payload.size() != 17) {
            throw protocol::ProtocolError("unit reply must contain 16 text bytes and one decimal byte");
        }
        UnitInfo info{};
        info.target_mi = target_mi_;
        info.unit = protocol::decode_ascii({payload.begin(), payload.begin() + 16});
        info.decimal_places = payload[16];
        for (size_t i = 0; i < info.raw.size() && i < payload.size(); ++i) {
            info.raw[i] = payload[i];
        }
        info.timestamp = client_->received_at();
        return info;
    }

    RangeInfo UnitManager::get_range(uint8_t si, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        protocol::require_positive_timeout(timeout_ms);
        if (si < 0x12 || si > 0x9E || (si - 0x12) % 4 != 0) {
            throw ValidationError("unit range SI must address one of the 36 low/high pairs");
        }
        const auto payload = client_->read(protocol::kMiUnit, si, 4, false, timeout_ms);
        if (payload.size() < 4) {
            throw protocol::ProtocolError("unit range reply is shorter than 4 bytes");
        }
        RangeInfo info{};
        info.target_mi = target_mi_;
        info.si = si;
        info.low = protocol::read_u16_le(payload.data());
        info.high = protocol::read_u16_le(payload.data() + 2);
        info.timestamp = client_->received_at();
        return info;
    }

    UnitInfo UnitManager::query(uint8_t mi, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        select_target(mi, timeout_ms);
        return get_unit(timeout_ms);
    }

} // namespace linkerbot::hand::o30
