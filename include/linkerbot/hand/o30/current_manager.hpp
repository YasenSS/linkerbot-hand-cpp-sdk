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

    // Writes change the running current limit. Treat them as a live command.
    class CurrentManager {
        friend class O30;

    public:
        explicit CurrentManager(Client &client);

        CurrentManager(const CurrentManager &) = delete;
        CurrentManager &operator=(const CurrentManager &) = delete;
        CurrentManager(CurrentManager &&) = delete;
        CurrentManager &operator=(CurrentManager &&) = delete;

        void set_current_limits(const std::array<uint8_t, kJointCount> &currents,
                                std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_all_current_limits(uint8_t current,
                                    std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] CurrentData get_currents(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        [[nodiscard]] CurrentLimitData
        get_current_limits(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<CurrentData> get_snapshot() const;
        [[nodiscard]] std::optional<CurrentLimitData> get_current_limit_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const CurrentData &)> sink);
        void set_limit_event_sink(std::function<void(const CurrentLimitData &)> sink);

        Client *client_;
        runtime::Cache<CurrentData> relay_;
        runtime::Cache<CurrentLimitData> limit_relay_;
    };

} // namespace linkerbot::hand::o30
