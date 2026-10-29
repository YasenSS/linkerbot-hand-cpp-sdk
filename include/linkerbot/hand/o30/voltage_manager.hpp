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

    class VoltageManager {
        friend class O30;

    public:
        explicit VoltageManager(Client &client);

        VoltageManager(const VoltageManager &) = delete;
        VoltageManager &operator=(const VoltageManager &) = delete;
        VoltageManager(VoltageManager &&) = delete;
        VoltageManager &operator=(VoltageManager &&) = delete;

        void set_voltages(const std::array<uint8_t, kJointCount> &voltages,
                          std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_all(uint8_t voltage, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] VoltageData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                                               ValueKind kind = ValueKind::kActual);

        [[nodiscard]] std::optional<VoltageData> get_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const VoltageData &)> sink);

        Client *client_;
        runtime::Cache<VoltageData> relay_;
    };

} // namespace linkerbot::hand::o30
