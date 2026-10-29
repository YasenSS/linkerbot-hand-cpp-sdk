#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/stall.hpp>
#include <linkerbot/hand/runtime/cache.hpp>

namespace linkerbot::hand::o30 {

    // MI 0x09 stall time, 0x0A threshold, 0x0B hold current after stall.
    class StallManager {
        friend class O30;

    public:
        explicit StallManager(Client &client);

        StallManager(const StallManager &) = delete;
        StallManager &operator=(const StallManager &) = delete;
        StallManager(StallManager &&) = delete;
        StallManager &operator=(StallManager &&) = delete;

        void set_times(const std::array<uint8_t, kJointCount> &times,
                       std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_thresholds(const std::array<uint8_t, kJointCount> &thresholds,
                            std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_hold_currents(const std::array<uint8_t, kJointCount> &currents,
                               std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] StallTimeData get_times(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                                              ValueKind kind = ValueKind::kActual);
        [[nodiscard]] StallThresholdData
        get_thresholds(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                       ValueKind kind = ValueKind::kActual);
        [[nodiscard]] StallHoldCurrentData
        get_hold_currents(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                          ValueKind kind = ValueKind::kActual);

        [[nodiscard]] std::optional<StallTimeData> get_times_snapshot() const;
        [[nodiscard]] std::optional<StallThresholdData> get_thresholds_snapshot() const;
        [[nodiscard]] std::optional<StallHoldCurrentData> get_hold_currents_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const StallTimeData &)> sink);

        Client *client_;
        runtime::Cache<StallTimeData> times_relay_;
        runtime::Cache<StallThresholdData> thresholds_relay_;
        runtime::Cache<StallHoldCurrentData> hold_relay_;
    };

} // namespace linkerbot::hand::o30
