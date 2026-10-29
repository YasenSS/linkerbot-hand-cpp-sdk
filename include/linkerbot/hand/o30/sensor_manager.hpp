#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/sensor.hpp>
#include <linkerbot/hand/runtime/cache.hpp>

namespace linkerbot::hand::o30 {

    // MI 0x31 metadata and 0x32..0x34 data channels. Channel reads are skipped
    // when 0x31 reports total_length == 0.
    class SensorManager {
        friend class O30;

    public:
        explicit SensorManager(Client &client);

        SensorManager(const SensorManager &) = delete;
        SensorManager &operator=(const SensorManager &) = delete;
        SensorManager(SensorManager &&) = delete;
        SensorManager &operator=(SensorManager &&) = delete;

        [[nodiscard]] SensorInfo get_info(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

        void select(uint8_t sensor_index, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        // channel is 1, 2 or 3 (MI 0x32..0x34). On 0.0.4 these are byte
        // banks [0,255), [255,511), [511,767). Throws for an absent bank.
        [[nodiscard]] SensorChannelData
        read_channel(uint8_t channel, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

        // Complete data for the selected sensor, including all 0.0.4 banks.
        // select/get_info/read operations on this manager are serialized.
        [[nodiscard]] SensorData read_data(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

        [[nodiscard]] std::optional<SensorData> get_snapshot() const;
        [[nodiscard]] SensorData read_all_data(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{1000});

    private:
        void set_event_sink(std::function<void(const SensorData &)> sink);
        SensorData read_selected(std::chrono::milliseconds timeout_ms);
        std::vector<uint8_t> read_bank(uint8_t channel, const SensorInfo &info, bool modern,
                                       std::chrono::milliseconds timeout_ms);
        std::recursive_mutex mutex_;
        Client *client_;
        runtime::Cache<SensorData> relay_;
    };

} // namespace linkerbot::hand::o30
