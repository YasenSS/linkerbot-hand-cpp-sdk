#include "linkerbot/hand/l30/fault_manager.hpp"
#include <chrono>
#include <linkerbot/hand/errors.hpp>
#include <stdexcept>
#include <utility>

namespace linkerbot::hand::l30 {

    FaultManager::FaultManager(Client &client) : client_(&client), relay_(client, "l30.fault") {
        report_handler_ = client_->add_report_handler(
            protocol::kSubFault, [this](const std::vector<uint8_t> &payload) { on_periodic_report(payload); });
    }

    FaultManager::~FaultManager() { close(); }

    FaultData FaultManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        const auto response = client_->read(protocol::kParentQuery, protocol::kSubFault, timeout_ms);
        FaultData data{protocol::decode_u8_response(response.data(), response.size()),
                       std::chrono::steady_clock::now()};
        relay_.push(data);
        return data;
    }

    std::optional<FaultData> FaultManager::get_snapshot() const { return relay_.snapshot(); }

    void FaultManager::close() {
        if (report_handler_.has_value()) {
            client_->remove_report_handler(protocol::kSubFault, *report_handler_);
            // Keep the immutable ID so concurrent close callers all cross
            // Client's cancellation barrier (including after self-cancel).
        }
        relay_.close();
    }

    void FaultManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void FaultManager::set_event_sink(std::function<void(const FaultData &)> sink) { relay_.set_sink(std::move(sink)); }

    void FaultManager::on_periodic_report(const std::vector<uint8_t> &payload) {
        const auto values =
            protocol::decode_periodic_u8_report(payload.data(), payload.size(), protocol::kPeriodicAllJointsMask);
        FaultData data{};
        for (size_t i = 0; i < kJointCount; ++i) {
            data.faults[i] = values[i];
        }
        data.timestamp = std::chrono::steady_clock::now();
        relay_.push(data);
    }

} // namespace linkerbot::hand::l30
