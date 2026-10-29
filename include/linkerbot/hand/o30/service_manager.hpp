#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/service.hpp>

namespace linkerbot::hand::o30 {

    class ServiceManager {
        friend class O30;

    public:
        explicit ServiceManager(Client &client);

        ServiceManager(const ServiceManager &) = delete;
        ServiceManager &operator=(const ServiceManager &) = delete;
        ServiceManager(ServiceManager &&) = delete;
        ServiceManager &operator=(ServiceManager &&) = delete;

        [[nodiscard]] ServiceStatus get_status(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{1000});

        [[nodiscard]] std::vector<uint8_t>
        read_field(uint8_t si, uint8_t edl, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

        void write_field(uint8_t si, const std::vector<uint8_t> &payload,
                         std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

        void set_hand_side(uint8_t side, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_sensor_type(uint8_t type, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_protocol_interface(uint8_t value,
                                    std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_password(const std::array<uint8_t, 8> &password,
                          std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});
        void set_device_uid(const std::vector<uint8_t> &uid,
                            std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

    private:
        Client *client_;
    };

} // namespace linkerbot::hand::o30
