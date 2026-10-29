#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/pid.hpp>

namespace linkerbot::hand::o30 {

    class PidManager {
        friend class O30;

    public:
        explicit PidManager(Client &client);

        PidManager(const PidManager &) = delete;
        PidManager &operator=(const PidManager &) = delete;
        PidManager(PidManager &&) = delete;
        PidManager &operator=(PidManager &&) = delete;

        [[nodiscard]] PidData get(PidTerm term, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                                  ValueKind kind = ValueKind::kActual);

        void set(PidTerm term, const std::array<uint8_t, kJointCount> &values,
                 std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

    private:
        Client *client_;
    };

} // namespace linkerbot::hand::o30
