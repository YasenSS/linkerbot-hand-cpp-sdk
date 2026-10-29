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

    class TemperatureManager {
        friend class O30;

    public:
        explicit TemperatureManager(Client &client);

        TemperatureManager(const TemperatureManager &) = delete;
        TemperatureManager &operator=(const TemperatureManager &) = delete;
        TemperatureManager(TemperatureManager &&) = delete;
        TemperatureManager &operator=(TemperatureManager &&) = delete;

        void set_temperatures(const std::array<uint8_t, kJointCount> &temperatures,
                              std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] TemperatureData
        get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                     ValueKind kind = ValueKind::kActual);

        [[nodiscard]] std::optional<TemperatureData> get_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const TemperatureData &)> sink);

        Client *client_;
        runtime::Cache<TemperatureData> relay_;
    };

} // namespace linkerbot::hand::o30
