#pragma once

#include <chrono>
#include <optional>

#include <linkerbot/hand/l30/cache.hpp>
#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/l30/force_sensor.hpp>

namespace linkerbot::hand::l30 {

    // Tactile matrix reads. Each finger comes back as a 12x6 byte matrix,
    // reassembled from the protocol's two-frame tactile response.
    class ForceSensorManager {
        friend class L30;

    public:
        explicit ForceSensorManager(Client &client);

        ForceSensorManager(const ForceSensorManager &) = delete;
        ForceSensorManager &operator=(const ForceSensorManager &) = delete;
        ForceSensorManager(ForceSensorManager &&) = delete;
        ForceSensorManager &operator=(ForceSensorManager &&) = delete;

        [[nodiscard]] FingerForceData
        get_finger(Finger finger, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{1000});

        // Reads all five fingers in turn. Only this call updates the snapshot;
        // a single-finger read does not.
        [[nodiscard]] AllFingersData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                      1000});

        [[nodiscard]] std::optional<AllFingersData> get_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const AllFingersData &)> sink);

        Client *client_;
        Cache<AllFingersData> relay_;
    };

} // namespace linkerbot::hand::l30
