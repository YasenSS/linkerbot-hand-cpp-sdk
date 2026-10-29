#include "linkerbot/hand/l30/current_manager.hpp"
#include <chrono>
#include <linkerbot/hand/errors.hpp>
#include <stdexcept>
#include <utility>

namespace linkerbot::hand::l30 {

    CurrentManager::CurrentManager(Client &client) : client_(&client), relay_(client, "l30.current") {
        report_handler_ = client_->add_report_handler(
            protocol::kSubCurrent, [this](const std::vector<uint8_t> &payload) { on_periodic_report(payload); });
    }

    CurrentManager::~CurrentManager() { close(); }

    CurrentData CurrentManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        const auto response = client_->read(protocol::kParentQuery, protocol::kSubCurrent, timeout_ms);
        CurrentData data{protocol::decode_i16_response(response.data(), response.size()),
                         std::chrono::steady_clock::now()};
        relay_.push(data);
        return data;
    }

    std::optional<CurrentData> CurrentManager::get_snapshot() const { return relay_.snapshot(); }

    void CurrentManager::close() {
        if (report_handler_.has_value()) {
            client_->remove_report_handler(protocol::kSubCurrent, *report_handler_);
            // Keep the immutable ID so concurrent close callers all cross
            // Client's cancellation barrier (including after self-cancel).
        }
        relay_.close();
    }

    void CurrentManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void CurrentManager::set_event_sink(std::function<void(const CurrentData &)> sink) {
        relay_.set_sink(std::move(sink));
    }

    void CurrentManager::on_periodic_report(const std::vector<uint8_t> &payload) {
        const auto values =
            protocol::decode_periodic_i16_report(payload.data(), payload.size(), protocol::kPeriodicAllJointsMask);
        CurrentData data{};
        for (size_t i = 0; i < kJointCount; ++i) {
            data.currents[i] = values[i];
        }
        data.timestamp = std::chrono::steady_clock::now();
        relay_.push(data);
    }

} // namespace linkerbot::hand::l30
