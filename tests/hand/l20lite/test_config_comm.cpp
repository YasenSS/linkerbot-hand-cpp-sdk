#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::Side;

namespace {

    constexpr uint8_t kSetIdCmd = 0xC3;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    FakeTransport::Handler config_handler(uint32_t id) {
        return [id](const Message &req) {
            if (req.dlc < 3 || req.data[0] != kSetIdCmd) {
                return std::vector<Message>{};
            }
            Message reply;
            reply.id = id;
            reply.dlc = 3;
            reply.data[0] = kSetIdCmd;
            reply.data[1] = (req.data[1] == 0x03) ? static_cast<uint8_t>(id) : req.data[2];
            reply.data[2] = (req.data[1] == 0x02) ? static_cast<uint8_t>(id) : req.data[2];
            if (req.data[1] == 0x01) {
                reply.data[1] = req.data[2];
                reply.data[2] = req.data[2];
            }
            return std::vector<Message>{reply};
        };
    }

} // namespace

// 新协议 0xC3：0x01 同时设收发、0x02 设备 ID、0x03 发送 ID，应答 [C3, device, tx]；
// 0xCC/0xCE/0xCF 为 7 字节填充。

// set_id(0x30) 发 [C3, 0x01, 0x30]，应答里 device/tx 都应是 0x30。
TEST_CASE("comm: config 0xC3 set_id uses subcommand 0x01 and waits", "[comm][l20lite][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, config_handler(l20lite_comm::kLeftId));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto ids = hand.config.set_id(0x30, kTimeout);
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{kSetIdCmd, 0x01, 0x30});
    REQUIRE(ids.device_id == 0x30);
    REQUIRE(ids.transmit_id == 0x30);
}

// set_device_id(0x31) 发 [C3, 0x02, 0x31]，读回 device_id=0x31。
TEST_CASE("comm: config 0xC3 set_device_id uses subcommand 0x02", "[comm][l20lite][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, config_handler(l20lite_comm::kLeftId));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto ids = hand.config.set_device_id(0x31, kTimeout);
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{kSetIdCmd, 0x02, 0x31});
    REQUIRE(ids.device_id == 0x31);
}

// set_transmit_id(0x32) 发 [C3, 0x03, 0x32]，读回 transmit_id=0x32。
TEST_CASE("comm: config 0xC3 set_transmit_id uses subcommand 0x03", "[comm][l20lite][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, config_handler(l20lite_comm::kLeftId));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto ids = hand.config.set_transmit_id(0x32, kTimeout);
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{kSetIdCmd, 0x03, 0x32});
    REQUIRE(ids.transmit_id == 0x32);
}

// 关应答 0xCC 填满 7 字节；开应答末字节为 0x01。
TEST_CASE("comm: config 0xCC reply enable/disable is 7-byte", "[comm][l20lite][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.set_reply_enabled(false);
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC});

    hand.config.set_reply_enabled(true);
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x01});
}

// restore_factory() 发 7 字节 0xCE 填充。
TEST_CASE("comm: config 0xCE restore factory is 7-byte fill", "[comm][l20lite][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.restore_factory();
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{0xCE, 0xCE, 0xCE, 0xCE, 0xCE, 0xCE, 0xCE});
}

// save() 发 7 字节 0xCF 填充。
TEST_CASE("comm: config 0xCF save is 7-byte fill", "[comm][l20lite][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.save();
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{0xCF, 0xCF, 0xCF, 0xCF, 0xCF, 0xCF, 0xCF});
}
