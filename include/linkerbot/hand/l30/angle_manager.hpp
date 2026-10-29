#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

#include <linkerbot/hand/l30/angle.hpp>
#include <linkerbot/hand/l30/cache.hpp>
#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/l30/joints.hpp>

namespace linkerbot::hand::l30 {

    // Joint position commands and position readback.
    //
    // set_angles takes protocol integers from kJointSpecs. Percentage commands
    // use set_angles_percent. Readback remains in percentage space; call
    // AngleData::angles.to_raw() to recover protocol counts.
    class AngleManager {
        friend class L30;

    public:
        explicit AngleManager(Client &client);
        ~AngleManager();

        AngleManager(const AngleManager &) = delete;
        AngleManager &operator=(const AngleManager &) = delete;
        AngleManager(AngleManager &&) = delete;
        AngleManager &operator=(AngleManager &&) = delete;

        void set_angles(const std::array<int16_t, kJointCount> &angles);
        void set_angles_percent(const Angle &angles);
        void set_angles_percent(const std::array<float, kJointCount> &percents);

        [[nodiscard]] AngleData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<AngleData> get_snapshot() const;

        // Stops routing periodic position reports into this manager.
        void close();

    private:
        void send_raw_angles(const std::array<int16_t, kJointCount> &raw_angles);
        void send_sense_request();
        void set_event_sink(std::function<void(const AngleData &)> sink);
        void on_periodic_report(const std::vector<uint8_t> &payload);

        Client *client_;
        Cache<AngleData> relay_;
        std::optional<Client::HandlerId> report_handler_;
    };

} // namespace linkerbot::hand::l30
