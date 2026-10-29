#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/o6/o6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::o6::O6;
using linkerbot::hand::o6::Side;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kSetIdCmd = 0xC3;
    constexpr uint8_t kFactoryCmd = 0xCE;
    constexpr uint8_t kSaveCmd = 0xCF;

    std::vector<uint8_t> last_payload(FakeTransport *transport) {
        const auto sent = transport->sent();
        REQUIRE_FALSE(sent.empty());
        const auto &msg = sent.back();
        return {msg.data.begin(), msg.data.begin() + msg.dlc};
    }

} // namespace

// 新协议 0xC3：0x01 同时设收发 ID，0x02 设备 ID，0x03 发送 ID；0xCE/0xCF 为 8 字节填充。

TEST_CASE("comm: config 0xC3 set_id uses subcommand 0x01", "[comm][o6][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.set_id(0x30);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kSetIdCmd, 0x01, 0x30});
}

TEST_CASE("comm: config 0xC3 set_device_id uses subcommand 0x02", "[comm][o6][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.set_device_id(0x31);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kSetIdCmd, 0x02, 0x31});
}

TEST_CASE("comm: config 0xC3 set_transmit_id uses subcommand 0x03", "[comm][o6][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.set_transmit_id(0x32);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kSetIdCmd, 0x03, 0x32});
}

TEST_CASE("comm: config 0xCE restore factory is 8-byte fill", "[comm][o6][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.restore_factory();
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kFactoryCmd, kFactoryCmd, kFactoryCmd, kFactoryCmd, kFactoryCmd,
                                                      kFactoryCmd, kFactoryCmd, kFactoryCmd});
}

TEST_CASE("comm: config 0xCF save is 8-byte fill", "[comm][o6][config]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.config.save();
    REQUIRE(last_payload(raw) ==
            std::vector<uint8_t>{kSaveCmd, kSaveCmd, kSaveCmd, kSaveCmd, kSaveCmd, kSaveCmd, kSaveCmd, kSaveCmd});
}
