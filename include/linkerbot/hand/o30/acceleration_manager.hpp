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

    class AccelerationManager {
        friend class O30;

    public:
        explicit AccelerationManager(Client &client);

        AccelerationManager(const AccelerationManager &) = delete;
        AccelerationManager &operator=(const AccelerationManager &) = delete;
        AccelerationManager(AccelerationManager &&) = delete;
        AccelerationManager &operator=(AccelerationManager &&) = delete;

        void set_accelerations(const std::array<uint8_t, kJointCount> &accelerations,
                               std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_all(uint8_t acceleration, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] AccelerationData
        get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                     ValueKind kind = ValueKind::kActual);

        [[nodiscard]] std::optional<AccelerationData> get_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const AccelerationData &)> sink);

        Client *client_;
        runtime::Cache<AccelerationData> relay_;
    };

} // namespace linkerbot::hand::o30
