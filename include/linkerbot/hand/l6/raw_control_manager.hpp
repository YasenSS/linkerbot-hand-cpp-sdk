#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include <linkerbot/hand/l6/detail/channel.hpp>
#include <linkerbot/hand/l6/joints.hpp>
#include <linkerbot/hand/protocol/transaction.hpp>

namespace linkerbot::hand::l6 {
    // Protocol counts, without physical-unit conversion. Readback can contain
    // values outside the documented write range and must retain that evidence.
    struct RawControlData {
        std::array<uint8_t, kJointCount> values{};
        std::chrono::steady_clock::time_point timestamp{};
        uint64_t sequence = 0;
        uint64_t generation = 0;
        std::chrono::microseconds latency{};
        bool command_response = false;
    };

    namespace protocol {
        ::linkerbot::hand::protocol::Progress raw_control_reply(uint32_t id, uint8_t opcode,
                                                                const can::Message &message, bool command_response);
        ::linkerbot::hand::protocol::TransactionPlan
        raw_control_transaction(uint32_t id, uint8_t opcode, const char *domain,
                                const std::optional<std::array<uint8_t, kJointCount>> &values = {});
    }

    class RawControlManager {
        friend class L6;

    public:
        RawControlManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime, uint8_t opcode,
                          const char *domain, uint8_t write_maximum = 255);
        void start();
        void stop();
        void set_values(const std::array<uint8_t, kJointCount> &values,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
        [[nodiscard]] RawControlData get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
        [[nodiscard]] std::optional<RawControlData> get_snapshot() const;

    private:
        uint32_t arbitration_id_;
        uint8_t opcode_;
        const char *domain_;
        uint8_t write_maximum_;
        detail::Channel<RawControlData> channel_;
    };
}
