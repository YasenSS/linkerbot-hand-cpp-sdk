#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/enable.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/runtime/cache.hpp>

namespace linkerbot::hand::o30 {

    // Compatibility stub: 0.0.3 enable masks do not control/reflect enable;
    // HOP 0.0.4 has no verified global enable command.
    // set_masks/get_blocking throw UnsupportedOperationError without sending a frame.
    class EnableManager {
        friend class O30;

    public:
        explicit EnableManager(Client &client);

        EnableManager(const EnableManager &) = delete;
        EnableManager &operator=(const EnableManager &) = delete;
        EnableManager(EnableManager &&) = delete;
        EnableManager &operator=(EnableManager &&) = delete;

        void set_masks(const std::array<uint8_t, kJointCount> &masks,
                       std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] EnableData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                                              ValueKind kind = ValueKind::kActual);

        [[nodiscard]] std::optional<EnableData> get_snapshot() const;

    private:
        Client *client_;
        runtime::Cache<EnableData> relay_;
    };

} // namespace linkerbot::hand::o30
