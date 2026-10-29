#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/motion.hpp>
#include <linkerbot/hand/runtime/cache.hpp>

namespace linkerbot::hand::o30 {

    class SpeedManager {
        friend class O30;

    public:
        explicit SpeedManager(Client &client);

        SpeedManager(const SpeedManager &) = delete;
        SpeedManager &operator=(const SpeedManager &) = delete;
        SpeedManager(SpeedManager &&) = delete;
        SpeedManager &operator=(SpeedManager &&) = delete;

        void set_speeds(const std::array<uint8_t, kJointCount> &speeds,
                        std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_all(uint8_t speed, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] SpeedData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                                             ValueKind kind = ValueKind::kActual);

        [[nodiscard]] std::optional<SpeedData> get_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const SpeedData &)> sink);

        Client *client_;
        runtime::Cache<SpeedData> relay_;
    };

} // namespace linkerbot::hand::o30
