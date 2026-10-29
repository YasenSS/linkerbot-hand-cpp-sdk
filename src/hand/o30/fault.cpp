#include "linkerbot/hand/o30/fault_manager.hpp"

#include "u8_io.hpp"

#include <utility>

#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    FaultManager::FaultManager(Client &client) : client_(&client), relay_(client.runtime(), "fault/relay_") {}

    void FaultManager::set_faults(const std::array<uint8_t, kJointCount> &, std::chrono::milliseconds) {
        throw UnsupportedOperationError("O30 fault clearing is not verified; raw write-back is not a clear command");
    }

    FaultData FaultManager::get_blocking(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        FaultData data{};
        data.revision = client_->revision(timeout_ms);
        const auto mi = data.revision == protocol::Revision::kV003 ? protocol::kMiFaultV003 : protocol::kMiFaultV004;
        data.faults = detail::read_joints(*client_, mi, kind, timeout_ms);
        data.kind = kind;
        data.timestamp = client_->received_at();
        relay_.push(data);
        return data;
    }

    std::optional<FaultData> FaultManager::get_snapshot() const { return relay_.snapshot(); }

    void FaultManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void FaultManager::set_event_sink(std::function<void(const FaultData &)> sink) { relay_.set_sink(std::move(sink)); }

} // namespace linkerbot::hand::o30
