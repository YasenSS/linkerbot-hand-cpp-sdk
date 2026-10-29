#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/events.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::SensorSource;

namespace {
    namespace proto = linkerbot::hand::l30::protocol;

    constexpr auto kTimeout = std::chrono::milliseconds{500};

    constexpr uint32_t kPeriodicPositionConfigId = 0x02802100;

    uint32_t periodic_config_id(uint8_t subcmd) {
        return l30_comm::request_id(proto::kParentPeriodic, subcmd, proto::kAccessWrite);
    }

    // 09 00 01 00 00 00 14 00 00 00 00 —— 使能、20 ms、全关节。
    std::vector<uint8_t> enable_config_bytes() {
        return {0x09, 0x00, 0x01, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00};
    }

    std::vector<uint8_t> disable_config_bytes() {
        return {0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }
} // namespace

// FakeTransport：L30 的构造与拆解——三个构造函数、auto_start_periodic 的默认
// 开关、stop() 的幂等性，以及自有 dispatcher 与注入 dispatcher 的归属差别。

// 传 auto_start_periodic=false 时构造完不应往总线上写任何东西。
TEST_CASE("comm: construction without auto periodic stays silent", "[comm][l30][lifecycle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE(raw->sent().empty());
    REQUIRE_NOTHROW(hand.stop());
}

// 默认 auto_start_periodic=true：恰好一帧周期配置，ID 0x02802100，
// 有效字节 09 00 01 00 00 00 14 00 00 00 00，DLC=16。
TEST_CASE("comm: auto periodic sends exactly one angle config frame", "[comm][l30][lifecycle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId);

    const auto sent = raw->sent();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].id == kPeriodicPositionConfigId);
    REQUIRE(sent[0].dlc == proto::kPeriodicConfigWireBytes);
    REQUIRE(l30_comm::useful(sent[0]) == enable_config_bytes());

    REQUIRE_NOTHROW(hand.stop());
}

// 没人回周期配置 ACK 时构造应超时抛 runtime_error（默认 100 ms）。
TEST_CASE("comm: construction fails when the periodic ACK never arrives", "[comm][l30][lifecycle]") {
    auto transport = std::make_unique<FakeTransport>();

    REQUIRE_THROWS_AS(L30(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId), std::runtime_error);
}

// 接口名构造走真 SocketCAN，离线只能验证不存在的接口会抛。
TEST_CASE("the interface-name constructor rejects a bogus interface", "[l30][lifecycle][socketcan]") {
    REQUIRE_THROWS_AS(L30("l30nosuchif0", l30_comm::kNodeId, l30_comm::kHostId, false), std::exception);
}

// 注入 dispatcher 的构造函数也能用；node_id/host_id 原样回读，含非默认值。
TEST_CASE("comm: the dispatcher constructor keeps the given addresses", "[comm][l30][lifecycle]") {
    constexpr uint8_t kNode = 5;
    constexpr uint8_t kHost = 9;

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    // install_periodic_acks() assumes the default host, so register the one
    // config ID this non-default pair actually uses.
    raw->on_request(
        l30_comm::request_id(proto::kParentPeriodic, proto::kSubPosition, proto::kAccessWrite, kNode, kHost),
        l30_comm::ack_handler());

    CANDispatcher dispatcher(std::move(transport));
    L30 hand(dispatcher, kNode, kHost);

    REQUIRE(hand.node_id() == kNode);
    REQUIRE(hand.host_id() == kHost);

    const auto sent = raw->sent();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].id ==
            l30_comm::request_id(proto::kParentPeriodic, proto::kSubPosition, proto::kAccessWrite, kNode, kHost));

    REQUIRE_NOTHROW(hand.stop());
    dispatcher.stop();
}

// stop() 调两次都应成功；离开作用域时析构再 stop 一次也不能抛。
TEST_CASE("comm: stop is idempotent and the destructor is quiet", "[comm][l30][lifecycle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);
    {
        L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId);
        REQUIRE_NOTHROW(hand.stop());
        REQUIRE_NOTHROW(hand.stop());
    }
    REQUIRE(true);
}

// stop() 要给每个开着的源都发一遍关闭配置：这里开了角度和速度两路。
TEST_CASE("comm: stop disables every source it enabled", "[comm][l30][lifecycle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    hand.report.configure(SensorSource::kAngle, true, proto::kPeriodicMinPeriodMs, proto::kPeriodicAllJointsMask,
                          kTimeout);
    hand.report.configure(SensorSource::kSpeed, true, proto::kPeriodicMinPeriodMs, proto::kPeriodicAllJointsMask,
                          kTimeout);
    raw->clear_sent();

    hand.stop();

    const auto position = l30_comm::frames_for(raw, kPeriodicPositionConfigId);
    const auto speed = l30_comm::frames_for(raw, periodic_config_id(proto::kSubSpeed));
    REQUIRE(position.size() == 1);
    REQUIRE(speed.size() == 1);
    REQUIRE(l30_comm::useful(position[0]) == disable_config_bytes());
    REQUIRE(l30_comm::useful(speed[0]) == disable_config_bytes());
}

// 自有 dispatcher 归本手所有，stop() 必须把底层 transport 关掉。
TEST_CASE("comm: stop closes an owned dispatcher", "[comm][l30][lifecycle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId);
    REQUIRE(raw->close_calls() == 0);

    hand.stop();
    REQUIRE(raw->close_calls() >= 1);
}

// 注入的 dispatcher 不归本手所有：stop() 之后 transport 不能被关，
// 同一总线上的第二只手还应能正常收发。
TEST_CASE("comm: stop leaves an injected dispatcher running", "[comm][l30][lifecycle]") {
    constexpr uint8_t kSecondNode = 2;

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw, l30_comm::kNodeId);
    l30_comm::install_periodic_acks(raw, kSecondNode);
    raw->on_request(l30_comm::query_id(proto::kSubPosition, kSecondNode),
                    l30_comm::i16_handler(l30_comm::filled_i16(450)));

    CANDispatcher dispatcher(std::move(transport));
    L30 first(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);
    L30 second(dispatcher, kSecondNode, l30_comm::kHostId);

    first.stop();
    REQUIRE(raw->close_calls() == 0);

    const auto data = second.angle.get_blocking(kTimeout);
    REQUIRE(data.angles.values[0] == 50.0F);

    second.stop();
    REQUIRE(raw->close_calls() == 0);
    dispatcher.stop();
}
