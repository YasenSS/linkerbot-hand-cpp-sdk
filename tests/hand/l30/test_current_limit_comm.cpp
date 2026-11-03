#include "hand/support/optional_value.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::kCurrentLimitMax;
using linkerbot::hand::l30::kCurrentLimitMin;
using linkerbot::hand::l30::kCurrentMilliampsPerCount;
using linkerbot::hand::l30::kJointCount;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::protocol::kSubCurrent;
using linkerbot::hand::l30::protocol::kSubCurrentLimit;
using linkerbot::hand::l30::protocol::kVectorWireBytes;

namespace {
    // 电流限制写命令 0x1/0x02/W；电流查询 0x5/0x02/R 共用子命令 0x02，只靠父命令区分。
    constexpr uint32_t kWriteId = 0x02204100U;
    constexpr uint32_t kCurrentQueryId = 0x00A04100U;

    std::array<uint16_t, kJointCount> filled_u16(uint16_t value) {
        std::array<uint16_t, kJointCount> values{};
        values.fill(value);
        return values;
    }

    // J1 取下限 60，J17 取上限 800，其余 200。
    std::array<uint16_t, kJointCount> sample_current_limits() {
        auto values = filled_u16(200);
        values[0] = kCurrentLimitMin;
        values[16] = kCurrentLimitMax;
        return values;
    }

    std::vector<uint8_t> sample_current_limit_bytes() {
        return {0x22, 0x00, 0x00, 0x3C, 0x00, 0xC8, 0x00, 0xC8, 0x00, 0xC8, 0x00, 0xC8,
                0x00, 0xC8, 0x00, 0xC8, 0x00, 0xC8, 0x00, 0xC8, 0x00, 0xC8, 0x00, 0xC8,
                0x00, 0xC8, 0x00, 0xC8, 0x00, 0xC8, 0x00, 0xC8, 0x00, 0xC8, 0x03, 0x20};
    }
} // namespace

// FakeTransport 电流限制：只写不读。写 0x02204100 发 34 字节大端 u16 向量，
// snapshot 存的是刚下发的命令值。

// set_current_limits → 一帧 0x02204100，22 00 + 17 个大端 u16，DLC 48。
TEST_CASE("comm: current limit encodes one vector frame", "[comm][l30][current-limit]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    hand.current_limit.set_current_limits(sample_current_limits());

    const auto frames = l30_comm::frames_for(raw, kWriteId);
    REQUIRE(frames.size() == 1);
    REQUIRE(l30_comm::useful(frames.front()) == sample_current_limit_bytes());
    REQUIRE(frames.front().dlc == kVectorWireBytes);
    REQUIRE(frames.front().is_extended);
    REQUIRE(frames.front().is_fd);
}

// set_all 与逐关节写同 ID 同字节。
TEST_CASE("comm: current limit set_all matches per-joint write", "[comm][l30][current-limit]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    hand.current_limit.set_all(200);
    hand.current_limit.set_current_limits(filled_u16(200));

    const auto frames = l30_comm::frames_for(raw, kWriteId);
    REQUIRE(frames.size() == 2);
    REQUIRE(l30_comm::useful(frames.front()) == l30_comm::useful(frames.back()));
}

// 边界 60 和 800 都该被接受。
TEST_CASE("comm: current limit accepts range boundaries", "[comm][l30][current-limit]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_NOTHROW(hand.current_limit.set_all(kCurrentLimitMin));
    REQUIRE_NOTHROW(hand.current_limit.set_all(kCurrentLimitMax));
    REQUIRE(l30_comm::count_id(raw, kWriteId) == 2);
}

// 59 和 801 越界 → invalid_argument，一个字节也不许上线。
TEST_CASE("comm: current limit rejects out of range counts", "[comm][l30][current-limit]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    auto low = filled_u16(200);
    low[0] = static_cast<uint16_t>(kCurrentLimitMin - 1);
    auto high = filled_u16(200);
    high[16] = static_cast<uint16_t>(kCurrentLimitMax + 1);

    REQUIRE_THROWS_AS(hand.current_limit.set_current_limits(low), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.current_limit.set_current_limits(high), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.current_limit.set_all(59), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.current_limit.set_all(801), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// 写成功后 snapshot 就是刚下发的命令值，且全程不发任何读请求。
TEST_CASE("comm: current limit snapshot holds commanded value", "[comm][l30][current-limit]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_FALSE(hand.current_limit.get_snapshot().has_value());
    hand.current_limit.set_current_limits(sample_current_limits());

    REQUIRE(hand.current_limit.get_snapshot().has_value());
    REQUIRE(test_support::require_value(hand.current_limit.get_snapshot()).current_limits == sample_current_limits());
    REQUIRE(l30_comm::count_id(raw, kWriteId) == 1);
    REQUIRE(raw->sent().size() == 1);
}

// 越界写被拒时 snapshot 不该被污染。
TEST_CASE("comm: current limit snapshot untouched by rejected write", "[comm][l30][current-limit]") {
    auto transport = std::make_unique<FakeTransport>();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.current_limit.set_all(0), linkerbot::hand::ValidationError);
    REQUIRE_FALSE(hand.current_limit.get_snapshot().has_value());
}

// 子命令 0x02 共用，但扭矩控制 0x02204100 与电流查询 0x00A04100 必须是两个 ID。
TEST_CASE("comm: current-limit control id differs from current query", "[comm][l30][current-limit]") {
    REQUIRE(kSubCurrentLimit == kSubCurrent);
    REQUIRE(l30_comm::write_id(kSubCurrentLimit) == kWriteId);
    REQUIRE(l30_comm::query_id(kSubCurrent) == kCurrentQueryId);
    REQUIRE(kWriteId != kCurrentQueryId);

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    hand.current_limit.set_all(200);
    REQUIRE(l30_comm::count_id(raw, kWriteId) == 1);
    REQUIRE(l30_comm::count_id(raw, kCurrentQueryId) == 0);
}

// 电流限制与实测电流共享 6.5 mA 每计数的标度：800 计数 ≈ 5200 mA。
TEST_CASE("comm: current-limit counts convert to milliamps", "[comm][l30][current-limit]") {
    REQUIRE(static_cast<float>(kCurrentLimitMax) * kCurrentMilliampsPerCount == Catch::Approx(5200.0F).margin(0.01));
    REQUIRE(static_cast<float>(kCurrentLimitMin) * kCurrentMilliampsPerCount == Catch::Approx(390.0F).margin(0.01));
}
