#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::protocol::ProtocolError;

namespace {

    namespace protocol = linkerbot::hand::l30::protocol;

    constexpr auto kTimeout = std::chrono::milliseconds{500};
    // 超时用例必须有界，取 50ms 让回归时立刻失败而不是挂住。
    constexpr auto kShortTimeout = std::chrono::milliseconds{50};

    // 实测过的使能/失能写命令 ID（DstID=1、SrcID=0）。
    constexpr uint32_t kEnableId = 0x0220E100;
    constexpr uint32_t kDisableId = 0x02210100;

    // 仓库不用 Catch2 matcher，异常文本一律自己抓出来 find()。
    bool throws_with(const std::function<void()> &call, const std::string &needle) {
        try {
            call();
        } catch (const std::exception &error) {
            return std::string(error.what()).find(needle) != std::string::npos;
        }
        return false;
    }

    int thrown_status(const std::function<void()> &call) {
        try {
            call();
        } catch (const ProtocolError &error) {
            return static_cast<int>(error.status());
        }
        return -1;
    }

} // namespace

// FakeTransport 全局使能/失能：control 父命令（0x1）下的 0x07/0x08 两个带 ACK 的写命令，
// 载荷都是空的 00 00、dlc=2；失败时设备用状态字说明原因（0x20 未解锁、0x23 状态不允许）。

// enable 只发一帧：ID 0x0220E100，有效字节 00 00，dlc=2。
TEST_CASE("comm: enable sends one empty control write", "[comm][l30][control]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kEnableId, l30_comm::ack_handler());

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE_NOTHROW(hand.control.enable(kTimeout));
    const auto sent = raw->sent();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent.front().id == kEnableId);
    REQUIRE(l30_comm::useful(sent.front()) == std::vector<uint8_t>{0x00, 0x00});
    REQUIRE(sent.front().dlc == protocol::kEmptyRequestBytes);
    REQUIRE(sent.front().is_extended);
    REQUIRE(sent.front().is_fd);
}

// disable 只发一帧：ID 0x02210100，有效字节 00 00，dlc=2。
TEST_CASE("comm: disable sends one empty control write", "[comm][l30][control]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kDisableId, l30_comm::ack_handler());

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE_NOTHROW(hand.control.disable(kTimeout));
    const auto sent = raw->sent();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent.front().id == kDisableId);
    REQUIRE(l30_comm::useful(sent.front()) == std::vector<uint8_t>{0x00, 0x00});
    REQUIRE(sent.front().dlc == protocol::kEmptyRequestBytes);
}

// ACK 状态字 0x20（未解锁）要原样带到 ProtocolError::status()。
TEST_CASE("comm: enable reports status 0x20 not unlocked", "[comm][l30][control]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kEnableId, l30_comm::ack_handler(0x20));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.control.enable(kTimeout), ProtocolError);
    REQUIRE(thrown_status([&hand] { hand.control.enable(kTimeout); }) == 0x20);
    REQUIRE(throws_with([&hand] { hand.control.enable(kTimeout); }, "L30 status 0x20"));
}

// ACK 状态字 0x23（当前状态不允许）同样要带回 status()。
TEST_CASE("comm: disable reports status 0x23 operation not allowed", "[comm][l30][control]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kDisableId, l30_comm::ack_handler(0x23));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.control.disable(kTimeout), ProtocolError);
    REQUIRE(thrown_status([&hand] { hand.control.disable(kTimeout); }) == 0x23);
    REQUIRE(throws_with([&hand] { hand.control.disable(kTimeout); }, "L30 status 0x23"));
}

// 非正超时先被参数校验挡住，一个字节都不许上总线。
TEST_CASE("comm: control rejects non-positive timeout", "[comm][l30][control]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kEnableId, l30_comm::ack_handler());
    raw->on_request(kDisableId, l30_comm::ack_handler());

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE_THROWS_AS(hand.control.enable(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.control.disable(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// 没有任何处理器时按超时报错，文本里要带上超时毫秒数。
TEST_CASE("comm: control timeout names the wait window", "[comm][l30][control]") {
    auto transport = std::make_unique<FakeTransport>();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE(throws_with([&hand] { hand.control.enable(kShortTimeout); }, "No L30 response received within 50ms"));
    REQUIRE(throws_with([&hand] { hand.control.enable(kShortTimeout); }, "quarantined"));
}
