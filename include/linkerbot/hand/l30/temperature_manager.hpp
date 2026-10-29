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

    // Per-joint temperature, 1 degree Celsius per count.
    class TemperatureManager {
        friend class L30;

    public:
        explicit TemperatureManager(Client &client);
        ~TemperatureManager();

        TemperatureManager(const TemperatureManager &) = delete;
        TemperatureManager &operator=(const TemperatureManager &) = delete;
        TemperatureManager(TemperatureManager &&) = delete;
        TemperatureManager &operator=(TemperatureManager &&) = delete;

        [[nodiscard]] TemperatureData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                       100});

        [[nodiscard]] std::optional<TemperatureData> get_snapshot() const;

        void close();

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const TemperatureData &)> sink);
        void on_periodic_report(const std::vector<uint8_t> &payload);

        Client *client_;
        Cache<TemperatureData> relay_;
        std::optional<Client::HandlerId> report_handler_;
    };

} // namespace linkerbot::hand::l30
