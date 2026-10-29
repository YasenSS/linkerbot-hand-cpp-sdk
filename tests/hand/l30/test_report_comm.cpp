#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/events.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::SensorSource;
using linkerbot::hand::l30::protocol::ProtocolError;

namespace {

    namespace protocol = linkerbot::hand::l30::protocol;

    constexpr auto kTimeout = std::chrono::milliseconds{500};

    // 实测过的周期上报配置 ID（DstID=1、SrcID=0），子命令来自被配置的数据源。
    constexpr uint32_t kAngleConfigId = 0x02802100;
    constexpr uint32_t kCurrentConfigId = 0x02804100;
    constexpr uint32_t kSpeedConfigId = 0x02806100;
    constexpr uint32_t kTemperatureConfigId = 0x02808100;
    constexpr uint32_t kFaultConfigId = 0x0280A100;

    // 使能 20ms 全关节：09 00 01 00 00 00 14 + 全零掩码；关闭时周期与掩码都清零。
    const std::vector<uint8_t> kEnable20msPayload{0x09, 0x00, 0x01, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00};
    const std::vector<uint8_t> kDisablePayload{0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

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

// FakeTransport 周期上报配置：periodic 父命令（0x4）下按数据源分子命令，载荷是 9 字节
// 「开关 + 大端周期 + 大端关节掩码」，补零到 16 字节上线。这些用例一律用
// auto_start_periodic=false 构造，否则构造函数自己那帧角度配置会混进 sent() 断言里。

// 五个合法数据源各自命中对应子命令的 ID，载荷是 20ms 全关节使能。
TEST_CASE("comm: report configure maps each source to its subcommand id", "[comm][l30][report]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const std::vector<std::pair<SensorSource, uint32_t>> cases{{SensorSource::kAngle, kAngleConfigId},
                                                               {SensorSource::kCurrent, kCurrentConfigId},
                                                               {SensorSource::kSpeed, kSpeedConfigId},
                                                               {SensorSource::kTemperature, kTemperatureConfigId},
                                                               {SensorSource::kFault, kFaultConfigId}};

    for (const auto &entry : cases) {
        raw->clear_sent();
        REQUIRE_NOTHROW(hand.report.configure(entry.first, true, protocol::kPeriodicMinPeriodMs,
                                              protocol::kPeriodicAllJointsMask, kTimeout));
        const auto sent = raw->sent();
        REQUIRE(sent.size() == 1);
        REQUIRE(sent.front().id == entry.second);
        REQUIRE(l30_comm::useful(sent.front()) == kEnable20msPayload);
        REQUIRE(sent.front().dlc == protocol::kPeriodicConfigWireBytes);
    }
}

// 触觉矩阵不能做周期上报，配置它报 does not support periodic reports。
TEST_CASE("comm: report rejects the force sensor source", "[comm][l30][report]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE_THROWS_AS(hand.report.configure(SensorSource::kForceSensor, true, protocol::kPeriodicMinPeriodMs,
                                            protocol::kPeriodicAllJointsMask, kTimeout),
                      linkerbot::hand::ValidationError);
    REQUIRE(throws_with(
        [&hand] {
            hand.report.configure(SensorSource::kForceSensor, true, protocol::kPeriodicMinPeriodMs,
                                  protocol::kPeriodicAllJointsMask, kTimeout);
        },
        "does not support periodic reports"));
    REQUIRE(raw->sent().empty());
}

// 管理器的快照是定长 17 项，所以使能时只接受全关节掩码（0），带位掩码要被拒。
TEST_CASE("comm: report rejects a partial joint mask when enabling", "[comm][l30][report]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE_THROWS_AS(hand.report.configure(SensorSource::kAngle, true, protocol::kPeriodicMinPeriodMs, 0x3U, kTimeout),
                      linkerbot::hand::ValidationError);
    REQUIRE(throws_with(
        [&hand] { hand.report.configure(SensorSource::kAngle, true, protocol::kPeriodicMinPeriodMs, 0x3U, kTimeout); },
        "only support all-joint periodic reports"));
    REQUIRE(raw->sent().empty());
}

// start_default 就是「角度、使能、20ms、全关节」：ID 0x02802100，11 个有效字节，dlc=16。
TEST_CASE("comm: report start_default enables angle at the minimum period", "[comm][l30][report]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE_NOTHROW(hand.report.start_default(kTimeout));
    const auto sent = raw->sent();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent.front().id == kAngleConfigId);
    REQUIRE(l30_comm::useful(sent.front()) == kEnable20msPayload);
    REQUIRE(sent.front().dlc == protocol::kPeriodicConfigWireBytes);
    REQUIRE(protocol::kPeriodicMinPeriodMs == 20);
}

// 周期低于 20ms 或高于 600000ms 都要被拒，且不许发帧。
TEST_CASE("comm: report rejects out-of-range periods", "[comm][l30][report]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE_THROWS_AS(hand.report.configure(SensorSource::kAngle, true, protocol::kPeriodicMinPeriodMs - 1,
                                            protocol::kPeriodicAllJointsMask, kTimeout),
                      linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.report.configure(SensorSource::kAngle, true, protocol::kPeriodicMaxPeriodMs + 1,
                                            protocol::kPeriodicAllJointsMask, kTimeout),
                      linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// disable_all 要先复制一份已使能集合再逐个关闭（configure 会边走边 erase）：
// 使能三个数据源就应看到三帧关闭配置。
TEST_CASE("comm: report disable_all turns off every enabled source", "[comm][l30][report]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    hand.report.configure(SensorSource::kAngle, true, protocol::kPeriodicMinPeriodMs, protocol::kPeriodicAllJointsMask,
                          kTimeout);
    hand.report.configure(SensorSource::kSpeed, true, protocol::kPeriodicMinPeriodMs, protocol::kPeriodicAllJointsMask,
                          kTimeout);
    hand.report.configure(SensorSource::kTemperature, true, protocol::kPeriodicMinPeriodMs,
                          protocol::kPeriodicAllJointsMask, kTimeout);
    raw->clear_sent();

    REQUIRE_NOTHROW(hand.report.disable_all(kTimeout));
    const auto sent = raw->sent();
    REQUIRE(sent.size() == 3);
    REQUIRE(l30_comm::sent_ids(raw) == std::vector<uint32_t>{kAngleConfigId, kSpeedConfigId, kTemperatureConfigId});
    for (const auto &msg : sent) {
        REQUIRE(l30_comm::useful(msg) == kDisablePayload);
        REQUIRE(msg.dlc == protocol::kPeriodicConfigWireBytes);
    }

    // 集合已清空，再来一次不该发出任何帧。
    raw->clear_sent();
    REQUIRE_NOTHROW(hand.report.disable_all(kTimeout));
    REQUIRE(raw->sent().empty());
}

// 配置被设备拒绝（0x34 周期上报配置失败）时抛 ProtocolError 并带回状态字。
TEST_CASE("comm: report configure propagates a failing ack status", "[comm][l30][report]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kAngleConfigId, l30_comm::ack_handler(0x34));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE_THROWS_AS(hand.report.start_default(kTimeout), ProtocolError);
    REQUIRE(thrown_status([&hand] { hand.report.start_default(kTimeout); }) == 0x34);
}

// 非正超时先被参数校验挡住，一个字节都不许上总线。
TEST_CASE("comm: report rejects non-positive timeout", "[comm][l30][report]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE_THROWS_AS(hand.report.start_default(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.report.configure(SensorSource::kFault, true, protocol::kPeriodicMinPeriodMs,
                                            protocol::kPeriodicAllJointsMask, std::chrono::milliseconds{-2}),
                      linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.report.disable(SensorSource::kAngle, std::chrono::milliseconds{0}),
                      linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}
