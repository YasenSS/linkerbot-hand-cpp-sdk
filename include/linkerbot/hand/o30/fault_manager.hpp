#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/fault.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/runtime/cache.hpp>

namespace linkerbot::hand::o30 {

    class FaultManager {
        friend class O30;

    public:
        explicit FaultManager(Client &client);

        FaultManager(const FaultManager &) = delete;
        FaultManager &operator=(const FaultManager &) = delete;
        FaultManager(FaultManager &&) = delete;
        FaultManager &operator=(FaultManager &&) = delete;

        // Compatibility stub; throws std::logic_error without protocol I/O.
        void set_faults(const std::array<uint8_t, kJointCount> &faults,
                        std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] FaultData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                                             ValueKind kind = ValueKind::kActual);

        [[nodiscard]] std::optional<FaultData> get_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const FaultData &)> sink);

        Client *client_;
        runtime::Cache<FaultData> relay_;
    };

} // namespace linkerbot::hand::o30
