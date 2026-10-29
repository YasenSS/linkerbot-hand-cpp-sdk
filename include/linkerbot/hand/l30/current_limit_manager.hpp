#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

#include <linkerbot/hand/l30/cache.hpp>
#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/motion.hpp>

namespace linkerbot::hand::l30 {

    // Per-joint current limits, in raw counts (60..800).
    //
    // There is no current-limit readback in the protocol, so the snapshot
    // holds the last target that was successfully sent. Measured effort comes
    // from CurrentManager.
    class CurrentLimitManager {
        friend class L30;

    public:
        explicit CurrentLimitManager(Client &client);

        CurrentLimitManager(const CurrentLimitManager &) = delete;
        CurrentLimitManager &operator=(const CurrentLimitManager &) = delete;
        CurrentLimitManager(CurrentLimitManager &&) = delete;
        CurrentLimitManager &operator=(CurrentLimitManager &&) = delete;

        void set_current_limits(const std::array<uint16_t, kJointCount> &current_limits);

        void set_all(uint16_t current_limit);

        // The last commanded target, not a measurement.
        [[nodiscard]] std::optional<CurrentLimitData> get_snapshot() const;

    private:
        void set_event_sink(std::function<void(const CurrentLimitData &)> sink);

        Client *client_;
        Cache<CurrentLimitData> relay_;
    };

} // namespace linkerbot::hand::l30
