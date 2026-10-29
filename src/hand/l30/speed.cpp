#include "linkerbot/hand/l30/speed_manager.hpp"
#include <chrono>
#include <linkerbot/hand/errors.hpp>
#include <stdexcept>
#include <utility>

namespace linkerbot::hand::l30 {

    SpeedManager::SpeedManager(Client &client)
        : client_(&client), relay_(client, "l30.speed"), configured_relay_(client, "l30.configured_speed") {
        report_handler_ = client_->add_report_handler(
            protocol::kSubSpeed, [this](const std::vector<uint8_t> &payload) { on_periodic_report(payload); });
    }

    SpeedManager::~SpeedManager() { close(); }

    void SpeedManager::set_speeds(const std::array<uint16_t, kJointCount> &speeds) {
        const auto call = client_->enter();
        validate_u16_range(speeds, kSpeedMin, kSpeedMax, "speeds");
        client_->send_no_ack(protocol::kParentControl, protocol::kSubSpeed, protocol::encode_u16_vector(speeds),
                             protocol::kVectorWireBytes);
        configured_relay_.push(SpeedData{speeds, std::chrono::steady_clock::now()});
    }

    void SpeedManager::set_all(uint16_t speed) {
        const auto call = client_->enter();
        std::array<uint16_t, kJointCount> speeds{};
        speeds.fill(speed);
        set_speeds(speeds);
    }

    SpeedData SpeedManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        const auto response = client_->read(protocol::kParentQuery, protocol::kSubSpeed, timeout_ms);
        const auto values = protocol::decode_i16_response(response.data(), response.size());
        SpeedData data{};
        // Speeds are unsigned on the wire; the shared i16 decoder is reused, so
        // widen back through uint16 rather than keeping the sign.
        for (size_t i = 0; i < kJointCount; ++i) {
            data.speeds[i] = static_cast<uint16_t>(values[i]);
        }
        data.timestamp = std::chrono::steady_clock::now();
        relay_.push(data);
        return data;
    }

    std::optional<SpeedData> SpeedManager::get_snapshot() const { return relay_.snapshot(); }

    std::optional<SpeedData> SpeedManager::get_configured_snapshot() const { return configured_relay_.snapshot(); }

    void SpeedManager::close() {
        if (report_handler_.has_value()) {
            client_->remove_report_handler(protocol::kSubSpeed, *report_handler_);
            // Keep the immutable ID so concurrent close callers all cross
            // Client's cancellation barrier (including after self-cancel).
        }
        relay_.close();
        configured_relay_.close();
    }

    void SpeedManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void SpeedManager::set_event_sink(std::function<void(const SpeedData &)> sink) { relay_.set_sink(std::move(sink)); }

    void SpeedManager::on_periodic_report(const std::vector<uint8_t> &payload) {
        const auto values =
            protocol::decode_periodic_i16_report(payload.data(), payload.size(), protocol::kPeriodicAllJointsMask);
        SpeedData data{};
        for (size_t i = 0; i < kJointCount; ++i) {
            data.speeds[i] = static_cast<uint16_t>(values[i]);
        }
        data.timestamp = std::chrono::steady_clock::now();
        relay_.push(data);
    }

} // namespace linkerbot::hand::l30
