#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <linkerbot/hand/l30/cache.hpp>
#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/l30/fault.hpp>

namespace linkerbot::hand::l30 {

    // Per-joint servo status bytes. A set bit means that condition is abnormal.
    class FaultManager {
        friend class L30;

    public:
        explicit FaultManager(Client &client);
        ~FaultManager();

        FaultManager(const FaultManager &) = delete;
        FaultManager &operator=(const FaultManager &) = delete;
        FaultManager(FaultManager &&) = delete;
        FaultManager &operator=(FaultManager &&) = delete;

        [[nodiscard]] FaultData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<FaultData> get_snapshot() const;

        void close();

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const FaultData &)> sink);
        void on_periodic_report(const std::vector<uint8_t> &payload);

        Client *client_;
        Cache<FaultData> relay_;
        std::optional<Client::HandlerId> report_handler_;
    };

} // namespace linkerbot::hand::l30
