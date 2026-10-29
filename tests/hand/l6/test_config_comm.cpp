#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l6/l6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l6::L6;
using linkerbot::hand::l6::Side;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kSetIdCmd = 0xC3;
    constexpr uint8_t kReplyCmd = 0xCC;
    constexpr uint8_t kFactoryCmd = 0xCE;
    constexpr uint8_t kSaveCmd = 0xCF;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    std::vector<uint8_t> last_payload(FakeTransport *transport) {
        const auto sent = transport->sent();
        REQUIRE_FALSE(sent.empty());
        const auto &msg = sent.back();
        return {msg.data.begin(), msg.data.begin() + msg.dlc};
    }

    FakeTransport::Handler config_handler(uint32_t id) {
        return [id](const Message &req) {
            if (req.dlc < 1 || req.data[0] != kSetIdCmd || req.dlc < 3) {
                return std::vector<Message>{};
            }
            Message reply;
            reply.id = id;
            reply.dlc = 3;
            reply.data[0] = kSetIdCmd;
            if (req.data[1] == 0x01) {
                reply.data[1] = req.data[2];
                reply.data[2] = static_cast<uint8_t>(id);
            } else {
                reply.data[1] = static_cast<uint8_t>(id);
                reply.data[2] = req.data[2];
            }
            return std::vector<Message>{reply};
        };
    }

} // namespace

TEST_CASE("comm: config 0xC3 set_id uses subcommand 0x01", "[comm][l6][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, config_handler(kLeftId));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto ids = hand.config.set_id(0x30, kTimeout);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kSetIdCmd, 0x01, 0x30});
    REQUIRE(ids.transmit_id == 0x30);
    REQUIRE(ids.receive_id == kLeftId);
}

TEST_CASE("comm: config 0xC3 set_receive_id uses subcommand 0x02", "[comm][l6][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, config_handler(kLeftId));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto ids = hand.config.set_receive_id(0x31, kTimeout);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kSetIdCmd, 0x02, 0x31});
    REQUIRE(ids.transmit_id == kLeftId);
    REQUIRE(ids.receive_id == 0x31);
}

TEST_CASE("comm: config 0xCC reply enable/disable", "[comm][l6][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.set_reply_enabled(false);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC});

    hand.config.set_reply_enabled(true);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x01});
}

TEST_CASE("comm: config 0xCE restore factory is 7-byte fill", "[comm][l6][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.restore_factory();
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{0xCE, 0xCE, 0xCE, 0xCE, 0xCE, 0xCE, 0xCE});
}

TEST_CASE("comm: config 0xCF save is 7-byte fill per new protocol", "[comm][l6][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.save();
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{0xCF, 0xCF, 0xCF, 0xCF, 0xCF, 0xCF, 0xCF});
}
