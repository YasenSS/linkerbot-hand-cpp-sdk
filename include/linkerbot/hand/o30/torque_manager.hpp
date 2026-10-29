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

    class TorqueManager {
        friend class O30;

    public:
        explicit TorqueManager(Client &client);

        TorqueManager(const TorqueManager &) = delete;
        TorqueManager &operator=(const TorqueManager &) = delete;
        TorqueManager(TorqueManager &&) = delete;
        TorqueManager &operator=(TorqueManager &&) = delete;

        void set_torque_limits(const std::array<uint8_t, kJointCount> &torques,
                               std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_all_torque_limits(uint8_t torque,
                                   std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] TorqueData get_torques(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        [[nodiscard]] TorqueLimitData
        get_torque_limits(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<TorqueData> get_snapshot() const;
        [[nodiscard]] std::optional<TorqueLimitData> get_torque_limit_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const TorqueData &)> sink);
        void set_limit_event_sink(std::function<void(const TorqueLimitData &)> sink);

        Client *client_;
        runtime::Cache<TorqueData> relay_;
        runtime::Cache<TorqueLimitData> limit_relay_;
    };

} // namespace linkerbot::hand::o30
