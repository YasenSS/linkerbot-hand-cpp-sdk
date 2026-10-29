#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <linkerbot/hand/l30/cache.hpp>
#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/l30/motion.hpp>

namespace linkerbot::hand::l30 {

    // Measured per-joint current, in raw counts (6.5 mA per count).
    //
    // Query and periodic report both use subcommand 0x02, the same value
    // current-limit control uses; they are told apart by their parent command.
    class CurrentManager {
        friend class L30;

    public:
        explicit CurrentManager(Client &client);
        ~CurrentManager();

        CurrentManager(const CurrentManager &) = delete;
        CurrentManager &operator=(const CurrentManager &) = delete;
        CurrentManager(CurrentManager &&) = delete;
        CurrentManager &operator=(CurrentManager &&) = delete;

        [[nodiscard]] CurrentData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<CurrentData> get_snapshot() const;

        void close();

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const CurrentData &)> sink);
        void on_periodic_report(const std::vector<uint8_t> &payload);

        Client *client_;
        Cache<CurrentData> relay_;
        std::optional<Client::HandlerId> report_handler_;
    };

} // namespace linkerbot::hand::l30
