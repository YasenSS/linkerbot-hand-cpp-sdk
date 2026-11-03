#include "hand/support/optional_value.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::kJointSpecs;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::raw_to_percent;

namespace {
    namespace proto = linkerbot::hand::l30::protocol;

    constexpr auto kTimeout = std::chrono::milliseconds{500};

    constexpr uint8_t kFirstNode = 1;
    constexpr uint8_t kSecondNode = 2;

    // Verified device -> host periodic report IDs; node 2 differs from node 1
    // only in SrcID, which is exactly what the fan-out has to key on.
    constexpr uint32_t kFirstReportId = 0x00802008;
    constexpr uint32_t kSecondReportId = 0x00802010;

    bool wait_until(const std::function<bool()> &pred, std::chrono::milliseconds limit = kTimeout) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        return pred();
    }
} // namespace

// FakeTransport：一条 CAN FD 总线上挂两只 L30（NodeID 1 / 2），
// 靠仲裁 ID 里的 SrcID 分流，stop() 只收自己那份、不动共享 dispatcher。

// 两只手共用一个 dispatcher，节点 1 的周期报告只进 1 号手，节点 2 只进 2 号手。
TEST_CASE("comm: a periodic report reaches only its own node", "[comm][l30][shared_bus]") {
    constexpr int16_t kFirstRaw = 300;
    constexpr int16_t kSecondRaw = 900;

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw, kFirstNode);
    l30_comm::install_periodic_acks(raw, kSecondNode);

    CANDispatcher dispatcher(std::move(transport));
    L30 first(dispatcher, kFirstNode, l30_comm::kHostId);
    L30 second(dispatcher, kSecondNode, l30_comm::kHostId);

    REQUIRE(l30_comm::report_id(proto::kSubPosition, kFirstNode) == kFirstReportId);
    REQUIRE(l30_comm::report_id(proto::kSubPosition, kSecondNode) == kSecondReportId);

    raw->inject(l30_comm::periodic_i16_frame(proto::kSubPosition, l30_comm::filled_i16(kFirstRaw), kFirstNode));
    REQUIRE(wait_until([&first] { return first.get_snapshot().angle.has_value(); }));
    REQUIRE_FALSE(second.get_snapshot().angle.has_value());

    raw->inject(l30_comm::periodic_i16_frame(proto::kSubPosition, l30_comm::filled_i16(kSecondRaw), kSecondNode));
    REQUIRE(wait_until([&second] { return second.get_snapshot().angle.has_value(); }));

    const auto first_snapshot = first.get_snapshot();
    const auto second_snapshot = second.get_snapshot();
    REQUIRE(first_snapshot.angle.has_value());
    REQUIRE(second_snapshot.angle.has_value());
    REQUIRE(test_support::require_value(first_snapshot.angle).angles.values[0] ==
            Catch::Approx(raw_to_percent(kFirstRaw, kJointSpecs[0])));
    REQUIRE(test_support::require_value(second_snapshot.angle).angles.values[0] ==
            Catch::Approx(raw_to_percent(kSecondRaw, kJointSpecs[0])));

    first.stop();
    second.stop();
    dispatcher.stop();
}

// 两只手的查询请求各带自己的 DstID，应答也只回给对应的那只手。
TEST_CASE("comm: two hands on one bus read their own responses", "[comm][l30][shared_bus]") {
    constexpr int16_t kFirstRaw = 300;
    constexpr int16_t kSecondRaw = 900;

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw, kFirstNode);
    l30_comm::install_periodic_acks(raw, kSecondNode);
    raw->on_request(l30_comm::query_id(proto::kSubPosition, kFirstNode),
                    l30_comm::i16_handler(l30_comm::filled_i16(kFirstRaw)));
    raw->on_request(l30_comm::query_id(proto::kSubPosition, kSecondNode),
                    l30_comm::i16_handler(l30_comm::filled_i16(kSecondRaw)));

    CANDispatcher dispatcher(std::move(transport));
    L30 first(dispatcher, kFirstNode, l30_comm::kHostId);
    L30 second(dispatcher, kSecondNode, l30_comm::kHostId);

    const auto first_angles = first.angle.get_blocking(kTimeout);
    const auto second_angles = second.angle.get_blocking(kTimeout);
    REQUIRE(first_angles.angles.values[0] == Catch::Approx(raw_to_percent(kFirstRaw, kJointSpecs[0])));
    REQUIRE(second_angles.angles.values[0] == Catch::Approx(raw_to_percent(kSecondRaw, kJointSpecs[0])));

    first.stop();
    second.stop();
    dispatcher.stop();
}

// 一只手 stop() 之后另一只照常工作，共享 dispatcher 也不能被关掉。
TEST_CASE("comm: stopping one hand leaves the shared bus alive", "[comm][l30][shared_bus]") {
    constexpr int16_t kSecondRaw = 900;

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw, kFirstNode);
    l30_comm::install_periodic_acks(raw, kSecondNode);
    raw->on_request(l30_comm::query_id(proto::kSubPosition, kSecondNode),
                    l30_comm::i16_handler(l30_comm::filled_i16(kSecondRaw)));

    CANDispatcher dispatcher(std::move(transport));
    L30 first(dispatcher, kFirstNode, l30_comm::kHostId);
    L30 second(dispatcher, kSecondNode, l30_comm::kHostId);

    first.stop();
    REQUIRE(raw->close_calls() == 0);
    REQUIRE(dispatcher.get_health().connected());

    const auto angles = second.angle.get_blocking(kTimeout);
    REQUIRE(angles.angles.values[0] == Catch::Approx(raw_to_percent(kSecondRaw, kJointSpecs[0])));

    raw->inject(l30_comm::periodic_i16_frame(proto::kSubPosition, l30_comm::filled_i16(kSecondRaw), kSecondNode));
    REQUIRE(wait_until([&second] { return second.get_snapshot().angle.has_value(); }));

    second.stop();
    dispatcher.stop();
}

// L30(dispatcher, ...) 这个构造函数本身：注入 FakeTransport 撑起的 dispatcher
// 也能正常收发，且拆解不影响 dispatcher。
TEST_CASE("comm: L30 accepts an injected FakeTransport-backed dispatcher", "[comm][l30][shared_bus]") {
    constexpr uint8_t kFill = 41;

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw, kFirstNode);
    raw->on_request(l30_comm::query_id(proto::kSubTemperature, kFirstNode),
                    l30_comm::u8_handler(l30_comm::filled_u8(kFill)));

    CANDispatcher dispatcher(std::move(transport));
    L30 hand(dispatcher, kFirstNode, l30_comm::kHostId);

    REQUIRE(hand.node_id() == kFirstNode);
    REQUIRE(hand.host_id() == l30_comm::kHostId);

    const auto data = hand.temperature.get_blocking(kTimeout);
    REQUIRE(data.temperatures[0] == kFill);
    REQUIRE(data.temperatures[16] == kFill);

    REQUIRE_NOTHROW(hand.stop());
    REQUIRE(raw->close_calls() == 0);
    dispatcher.stop();
}
