#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <linkerbot/hand/l30/cache.hpp>
#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/motion.hpp>

namespace linkerbot::hand::l30 {

    // Per-joint speed targets and speed readback, in raw counts (1..250).
    class SpeedManager {
        friend class L30;

    public:
        explicit SpeedManager(Client &client);
        ~SpeedManager();

        SpeedManager(const SpeedManager &) = delete;
        SpeedManager &operator=(const SpeedManager &) = delete;
        SpeedManager(SpeedManager &&) = delete;
        SpeedManager &operator=(SpeedManager &&) = delete;

        void set_speeds(const std::array<uint16_t, kJointCount> &speeds);

        // Same target for every joint.
        void set_all(uint16_t speed);

        [[nodiscard]] SpeedData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<SpeedData> get_snapshot() const;

        // Latest successfully dispatched target, not device-confirmed or measured.
        // Empty before the first write; never sends a device query.
        [[nodiscard]] std::optional<SpeedData> get_configured_snapshot() const;

        void close();

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const SpeedData &)> sink);
        void on_periodic_report(const std::vector<uint8_t> &payload);

        Client *client_;
        Cache<SpeedData> relay_;
        Cache<SpeedData> configured_relay_;
        std::optional<Client::HandlerId> report_handler_;
    };

} // namespace linkerbot::hand::l30
