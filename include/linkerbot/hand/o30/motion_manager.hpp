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

    // MI 0x08 motion time. Counts are 10 ms each.
    class MotionManager {
        friend class O30;

    public:
        explicit MotionManager(Client &client);

        MotionManager(const MotionManager &) = delete;
        MotionManager &operator=(const MotionManager &) = delete;
        MotionManager(MotionManager &&) = delete;
        MotionManager &operator=(MotionManager &&) = delete;

        void set_counts(const std::array<uint8_t, kJointCount> &counts,
                        std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_all_counts(uint8_t count, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_milliseconds(const std::array<uint32_t, kJointCount> &milliseconds,
                              std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] MotionTimeData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                                                  ValueKind kind = ValueKind::kActual);

        [[nodiscard]] std::optional<MotionTimeData> get_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const MotionTimeData &)> sink);

        Client *client_;
        runtime::Cache<MotionTimeData> relay_;
    };

} // namespace linkerbot::hand::o30
