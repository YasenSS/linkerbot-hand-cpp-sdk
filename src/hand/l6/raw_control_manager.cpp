#include <linkerbot/hand/l6/protocol_backend.hpp>
#include <linkerbot/hand/l6/raw_control_manager.hpp>

namespace linkerbot::hand::l6 {
    namespace protocol {
        common::Progress raw_control_reply(uint32_t id, uint8_t opcode, const can::Message &message,
                                           bool command_response) {
            if (!is_classic_response(message, id, 7, opcode)) {
                return common::Progress::unmatched();
            }
            RawControlData data;
            std::copy_n(message.data.begin() + 1, kJointCount, data.values.begin());
            data.command_response = command_response;
            // Progress owns this payload through std::any; covered by ownership regression tests.
            // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
            return common::Progress::complete(data, command_response ? common::Origin::command_feedback
                                                                     : common::Origin::response);
        }

        common::TransactionPlan raw_control_transaction(uint32_t id, uint8_t opcode, const char *domain,
                                                        const std::optional<std::array<uint8_t, kJointCount>> &values) {
            auto plan = command(id, opcode,
                                values ? std::vector<uint8_t>(values->begin(), values->end()) : std::vector<uint8_t>{});
            plan.domain = domain;
            plan.keys = {domain};
            if (values) {
                plan.command_family = domain;
            }
            plan.consume = [id, opcode, writing = values.has_value()](const can::Message &message) {
                return raw_control_reply(id, opcode, message, writing);
            };
            return plan;
        }
    }

    RawControlManager::RawControlManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime, uint8_t opcode,
                                         const char *domain, uint8_t write_maximum)
        : arbitration_id_(id), opcode_(opcode), domain_(domain), write_maximum_(write_maximum),
          channel_(std::move(runtime), domain) {}

    void RawControlManager::start() { channel_.start(); }
    void RawControlManager::stop() { channel_.stop(); }

    void RawControlManager::set_values(const std::array<uint8_t, kJointCount> &values,
                                       std::chrono::milliseconds timeout) {
        const auto call = channel_.enter();
        if (std::any_of(values.begin(), values.end(), [this](uint8_t value) { return value > write_maximum_; })) {
            throw ValidationError("L6 control value exceeds documented write range");
        }
        // Firmware may reply with the pre-write setting. Preserve the actual
        // response, rather than publishing the requested values as readback.
        static_cast<void>(channel_.get(protocol::raw_control_transaction(arbitration_id_, opcode_, domain_, values),
                                       runtime::deadline_after(timeout)));
    }

    RawControlData RawControlManager::get_blocking(std::chrono::milliseconds timeout) {
        const auto call = channel_.enter();
        return channel_.get(protocol::raw_control_transaction(arbitration_id_, opcode_, domain_),
                            runtime::deadline_after(timeout));
    }

    std::optional<RawControlData> RawControlManager::get_snapshot() const { return channel_.snapshot(); }
}
