#include "linkerbot/hand/l30/force_sensor.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l30/force_sensor_manager.hpp>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace linkerbot::hand::l30 {

    std::string_view finger_name(Finger finger) {
        switch (finger) {
        case Finger::kThumb:
            return "thumb";
        case Finger::kIndex:
            return "index";
        case Finger::kMiddle:
            return "middle";
        case Finger::kRing:
            return "ring";
        case Finger::kPinky:
            return "pinky";
        }
        throw ValidationError("unknown L30 finger");
    }

    uint8_t finger_subcommand(Finger finger) {
        switch (finger) {
        case Finger::kThumb:
            return protocol::kSubTactileThumb;
        case Finger::kIndex:
            return protocol::kSubTactileIndex;
        case Finger::kMiddle:
            return protocol::kSubTactileMiddle;
        case Finger::kRing:
            return protocol::kSubTactileRing;
        case Finger::kPinky:
            return protocol::kSubTactilePinky;
        }
        throw ValidationError("unknown L30 finger");
    }

    FingerForceData FingerForceData::from_payload(Finger finger,
                                                  const std::array<uint8_t, protocol::kTactileDataLength> &payload,
                                                  std::chrono::steady_clock::time_point timestamp) {
        FingerForceData data{};
        data.finger = finger;
        data.timestamp = timestamp;
        // The 72 bytes arrive row-major: six columns per row, twelve rows.
        for (size_t row = 0; row < kRows; ++row) {
            for (size_t column = 0; column < kColumns; ++column) {
                data.values[row][column] = payload[(row * kColumns) + column];
            }
        }
        return data;
    }

    ForceSensorManager::ForceSensorManager(Client &client) : client_(&client), relay_(client, "l30.force_sensor") {}

    FingerForceData ForceSensorManager::get_finger(Finger finger, std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        const auto payload = client_->read_tactile(finger_subcommand(finger), timeout_ms);
        return FingerForceData::from_payload(finger, payload, std::chrono::steady_clock::now());
    }

    AllFingersData ForceSensorManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        const auto operation = client_->operation(timeout_ms, true);
        AllFingersData data{};
        data.thumb = get_finger(Finger::kThumb, operation.remaining());
        data.index = get_finger(Finger::kIndex, operation.remaining());
        data.middle = get_finger(Finger::kMiddle, operation.remaining());
        data.ring = get_finger(Finger::kRing, operation.remaining());
        data.pinky = get_finger(Finger::kPinky, operation.remaining());
        data.timestamp = std::chrono::steady_clock::now();
        relay_.push(data);
        return data;
    }

    std::optional<AllFingersData> ForceSensorManager::get_snapshot() const { return relay_.snapshot(); }

    void ForceSensorManager::set_event_sink(std::function<void(const AllFingersData &)> sink) {
        relay_.set_sink(std::move(sink));
    }

    void ForceSensorManager::send_sense_request() { static_cast<void>(get_blocking()); }

} // namespace linkerbot::hand::l30
