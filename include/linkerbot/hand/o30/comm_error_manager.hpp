#pragma once

#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/comm_error.hpp>

namespace linkerbot::hand::o30 {

    class CommErrorManager {
        friend class O30;

    public:
        explicit CommErrorManager(Client &client);

        CommErrorManager(const CommErrorManager &) = delete;
        CommErrorManager &operator=(const CommErrorManager &) = delete;
        CommErrorManager(CommErrorManager &&) = delete;
        CommErrorManager &operator=(CommErrorManager &&) = delete;

        [[nodiscard]] uint8_t get_latest_index(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] CommErrorData get_history(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

    private:
        Client *client_;
    };

} // namespace linkerbot::hand::o30
