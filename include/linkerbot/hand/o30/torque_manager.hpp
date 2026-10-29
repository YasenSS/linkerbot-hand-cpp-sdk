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

        [[nodiscard]] TorqueData get_torques(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<TorqueData> get_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const TorqueData &)> sink);

        Client *client_;
        runtime::Cache<TorqueData> relay_;
    };

} // namespace linkerbot::hand::o30
