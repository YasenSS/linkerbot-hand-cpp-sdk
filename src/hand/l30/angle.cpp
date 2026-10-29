#include "linkerbot/hand/l30/angle.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l30/angle_manager.hpp>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace linkerbot::hand::l30 {

    namespace {
        void check_timeout(std::chrono::milliseconds timeout_ms) {
            if (timeout_ms.count() <= 0) {
                throw ValidationError("timeout_ms must be positive");
            }
        }
    } // namespace

    Angle Angle::from_array(const std::array<float, kJointCount> &values) {
        validate_percents(values, "angles");
        return Angle{values};
    }

    Angle Angle::from_sensor_raw(const std::array<int16_t, kJointCount> &raw) { return Angle{raw_to_percents(raw)}; }

    AngleManager::AngleManager(Client &client) : client_(&client), relay_(client, "l30.angle") {
        report_handler_ = client_->add_report_handler(
            protocol::kSubPosition, [this](const std::vector<uint8_t> &payload) { on_periodic_report(payload); });
    }

    AngleManager::~AngleManager() { close(); }

    void AngleManager::close() {
        if (report_handler_.has_value()) {
            client_->remove_report_handler(protocol::kSubPosition, *report_handler_);
            // Keep the immutable ID so concurrent close callers all cross
            // Client's cancellation barrier (including after self-cancel).
        }
        relay_.close();
    }

    void AngleManager::set_angles(const std::array<int16_t, kJointCount> &angles) {
        const auto call = client_->enter();
        validate_raw_commands(angles, "angles");
        send_raw_angles(angles);
    }

    void AngleManager::set_angles_percent(const Angle &angles) {
        const auto call = client_->enter();
        angles.validate();
        send_raw_angles(angles.to_raw());
    }

    void AngleManager::set_angles_percent(const std::array<float, kJointCount> &percents) {
        const auto call = client_->enter();
        set_angles_percent(Angle::from_array(percents));
    }

    AngleData AngleManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        check_timeout(timeout_ms);
        const auto response = client_->read(protocol::kParentQuery, protocol::kSubPosition, timeout_ms);
        const AngleData data{Angle::from_sensor_raw(protocol::decode_i16_response(response.data(), response.size())),
                             std::chrono::steady_clock::now()};
        relay_.push(data);
        return data;
    }

    std::optional<AngleData> AngleManager::get_snapshot() const { return relay_.snapshot(); }

    void AngleManager::set_event_sink(std::function<void(const AngleData &)> sink) { relay_.set_sink(std::move(sink)); }

    void AngleManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void AngleManager::send_raw_angles(const std::array<int16_t, kJointCount> &raw_angles) {
        client_->send_no_ack(protocol::kParentControl, protocol::kSubPosition, protocol::encode_i16_vector(raw_angles),
                             protocol::kVectorWireBytes);
    }

    void AngleManager::on_periodic_report(const std::vector<uint8_t> &payload) {
        const auto values =
            protocol::decode_periodic_i16_report(payload.data(), payload.size(), protocol::kPeriodicAllJointsMask);
        std::array<int16_t, kJointCount> raw{};
        for (size_t i = 0; i < kJointCount; ++i) {
            raw[i] = values[i];
        }
        relay_.push(AngleData{Angle::from_sensor_raw(raw), std::chrono::steady_clock::now()});
    }

} // namespace linkerbot::hand::l30
