#pragma once

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/api/device.hpp>
#include <linkerbot/hand/errors.hpp>
#include <memory>
#include <stdexcept>

// Offline endpoint using the real L6 codec, identity verification and runtime.
namespace validation {
    using namespace linkerbot::hand;
    using linkerbot::can::FakeTransport;
    using linkerbot::can::Message;

    inline void require(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }

    inline std::vector<Message> l6_reply(const Message &request) {
        if (request.data[0] == 0xC0) {
            const std::string code = "LHL6-1-123-L";
            std::vector<Message> replies;
            for (size_t chunk = 0; chunk < 4; ++chunk) {
                Message reply{};
                reply.id = request.id;
                reply.dlc = 8;
                reply.data[0] = 0xC0;
                reply.data[1] = static_cast<uint8_t>(chunk);
                for (size_t i = 0; i < 6 && chunk * 6 + i < code.size(); ++i) {
                    reply.data[i + 2] = static_cast<uint8_t>(code[chunk * 6 + i]);
                }
                replies.push_back(reply);
            }
            return replies;
        }
        auto reply = request;
        const auto command = request.data[0];
        reply.dlc = command == 0xC1 ? 5 : (command == 0xC2 || command == 0xC4) ? 4 : 7;
        for (size_t i = 1; i < reply.dlc; ++i) {
            reply.data[i] = request.dlc > 1 ? request.data[i] : uint8_t{64};
        }
        return {reply};
    }

    inline HandDeviceConfig config(size_t capacity = 16, int timeout_ms = 3000) {
        HandDeviceConfig result;
        result.model = Model::kL6;
        result.side = HandSide::kLeft;
        result.interface_name = "offline-validation";
        result.angle_mapping_path = std::filesystem::temp_directory_path() / "linkerbot-m5-angle-mapping.toml";
        result.discrete_queue_capacity = capacity;
        result.command_timeout = std::chrono::milliseconds{timeout_ms};
        return result;
    }

    struct FakeHand {
        FakeTransport *transport = nullptr;
        HandDevice hand;
        explicit FakeHand(HandDeviceConfig cfg = config())
            : hand(std::move(cfg), [this] {
                  auto next = std::make_unique<FakeTransport>();
                  transport = next.get();
                  next->on_request(0x28, l6_reply);
                  return next;
              }) {
            hand.open();
        }
        ~FakeHand() {
            if (hand.is_open()) {
                transport->release_tx_gate();
            }
            try {
                hand.close();
            } catch (...) {
            }
        }
        CommandHandle block() {
            transport->arm_tx_gate();
            auto command = hand.set_angles(std::vector<int32_t>(6, 10));
            require(transport->wait_until_tx_entered(std::chrono::seconds{2}), "send gate not entered");
            require(!command.done(), "setter waited for transport");
            return command;
        }
    };

    inline void complete(const CommandHandle &handle) {
        require(handle.wait_for(std::chrono::seconds{4}), "command did not terminate");
        if (auto error = handle.error()) {
            std::rethrow_exception(error);
        }
        require(handle.stage() == CommandStage::kAcknowledged ||
                    (handle.stage() == CommandStage::kDispatched && handle.ack_expectation() == AckExpectation::kNone),
                "unsuccessful command stage");
    }
}
