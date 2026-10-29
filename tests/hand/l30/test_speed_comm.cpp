#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::kJointCount;
using linkerbot::hand::l30::kSpeedMax;
using linkerbot::hand::l30::kSpeedMin;
using linkerbot::hand::l30::kSpeedRpmPerCount;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::protocol::kEmptyRequestBytes;
using linkerbot::hand::l30::protocol::kSubSpeed;
using linkerbot::hand::l30::protocol::kVectorWireBytes;
using linkerbot::hand::l30::protocol::ProtocolError;

namespace {
    constexpr auto kTimeout = l30_comm::kTimeout;

    // 速度写命令 0x1/0x03/W 与速度查询 0x5/0x03/R 的真实仲裁 ID。
    constexpr uint32_t kWriteId = 0x02206100U;
    constexpr uint32_t kQueryId = 0x00A06100U;
    constexpr uint32_t kReportId = 0x00806008U;

    template <typename Ready> bool wait_until(Ready ready) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (ready()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return false;
    }

    std::array<uint16_t, kJointCount> filled_u16(uint16_t value) {
        std::array<uint16_t, kJointCount> values{};
        values.fill(value);
        return values;
    }

    // J1 取下限 1，J17 取上限 250，其余 100，覆盖整个合法区间。
    std::array<uint16_t, kJointCount> sample_speeds() {
        auto values = filled_u16(100);
        values[0] = kSpeedMin;
        values[16] = kSpeedMax;
        return values;
    }

    std::vector<uint8_t> sample_speed_bytes() {
        return {0x22, 0x00, 0x00, 0x01, 0x00, 0x64, 0x00, 0x64, 0x00, 0x64, 0x00, 0x64,
                0x00, 0x64, 0x00, 0x64, 0x00, 0x64, 0x00, 0x64, 0x00, 0x64, 0x00, 0x64,
                0x00, 0x64, 0x00, 0x64, 0x00, 0x64, 0x00, 0x64, 0x00, 0x64, 0x00, 0xFA};
    }

    // 上报/应答走共用的 i16 解码器，所以按 int16 造原始计数。
    std::array<int16_t, kJointCount> sample_counts() {
        std::array<int16_t, kJointCount> values{};
        for (size_t i = 0; i < kJointCount; ++i) {
            values[i] = static_cast<int16_t>(sample_speeds()[i]);
        }
        return values;
    }
} // namespace

// FakeTransport 速度：写 0x02206100 发 34 字节大端 u16 向量，读 0x00A06100 空载荷；
// API 一律用原始计数（1..250），要 rpm 自己乘 kSpeedRpmPerCount。

// set_speeds → 一帧 0x02206100，22 00 + 17 个大端 u16，DLC 48。
TEST_CASE("comm: speed set_speeds encodes one vector frame", "[comm][l30][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    hand.speed.set_speeds(sample_speeds());

    const auto frames = l30_comm::frames_for(raw, kWriteId);
    REQUIRE(frames.size() == 1);
    REQUIRE(l30_comm::useful(frames.front()) == sample_speed_bytes());
    REQUIRE(frames.front().dlc == kVectorWireBytes);
    REQUIRE(frames.front().is_extended);
    REQUIRE(frames.front().is_fd);
}

// set_all 与逐关节写同 ID 同字节。
TEST_CASE("comm: speed set_all matches per-joint write", "[comm][l30][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    hand.speed.set_all(100);
    hand.speed.set_speeds(filled_u16(100));

    const auto frames = l30_comm::frames_for(raw, kWriteId);
    REQUIRE(frames.size() == 2);
    REQUIRE(l30_comm::useful(frames.front()) == l30_comm::useful(frames.back()));
}

TEST_CASE("comm: L30 configured speeds cache dispatch targets independently", "[comm][l30][speed][configured-speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(l30_comm::filled_i16(7)));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_FALSE(hand.speed.get_configured_snapshot());
    REQUIRE_FALSE(hand.get_snapshot().configured_speeds);
    REQUIRE(raw->sent().empty());
    hand.speed.set_speeds(sample_speeds());
    const auto configured = hand.speed.get_configured_snapshot();
    REQUIRE(configured->speeds == sample_speeds());
    REQUIRE(hand.get_snapshot().configured_speeds->speeds == sample_speeds());
    REQUIRE_FALSE(hand.speed.get_snapshot());
    REQUIRE(raw->sent().size() == 1);

    REQUIRE(hand.speed.get_blocking(kTimeout).speeds == filled_u16(7));
    raw->inject(l30_comm::periodic_i16_frame(kSubSpeed, l30_comm::filled_i16(9)));
    REQUIRE(wait_until([&] { return hand.speed.get_snapshot()->speeds == filled_u16(9); }));
    REQUIRE(hand.speed.get_configured_snapshot()->speeds == sample_speeds());
    REQUIRE(hand.speed.get_configured_snapshot()->timestamp == configured->timestamp);

    REQUIRE_THROWS_AS(hand.speed.set_all(0), linkerbot::hand::ValidationError);
    REQUIRE(hand.speed.get_configured_snapshot()->speeds == sample_speeds());
    hand.speed.set_all(250);
    REQUIRE(hand.speed.get_configured_snapshot()->speeds == filled_u16(250));
    raw->fail_writes("configured speed write failed");
    REQUIRE_THROWS(hand.speed.set_all(1));
    REQUIRE(hand.speed.get_configured_snapshot()->speeds == filled_u16(250));
}

// 边界 1 和 250 都该被接受。
TEST_CASE("comm: speed accepts range boundaries", "[comm][l30][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_NOTHROW(hand.speed.set_all(kSpeedMin));
    REQUIRE_NOTHROW(hand.speed.set_all(kSpeedMax));
    REQUIRE(l30_comm::count_id(raw, kWriteId) == 2);
}

// 0 和 251 越界 → invalid_argument，一个字节也不许上线。
TEST_CASE("comm: speed rejects out of range counts", "[comm][l30][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    auto low = filled_u16(100);
    low[0] = static_cast<uint16_t>(kSpeedMin - 1);
    auto high = filled_u16(100);
    high[16] = static_cast<uint16_t>(kSpeedMax + 1);

    REQUIRE_THROWS_AS(hand.speed.set_speeds(low), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.set_speeds(high), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.set_all(0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.set_all(251), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// get_blocking 发空载荷查询帧，读回的是原始计数并刷新 snapshot。
TEST_CASE("comm: speed get_blocking decodes counts", "[comm][l30][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(sample_counts()));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_FALSE(hand.speed.get_snapshot().has_value());
    const auto data = hand.speed.get_blocking(kTimeout);

    const auto frames = l30_comm::frames_for(raw, kQueryId);
    REQUIRE(frames.size() == 1);
    REQUIRE(l30_comm::trimmed(frames.front()) == std::vector<uint8_t>{0x00, 0x00});
    REQUIRE(frames.front().dlc == kEmptyRequestBytes);
    REQUIRE(data.speeds == sample_speeds());
    REQUIRE(hand.speed.get_snapshot().has_value());
    REQUIRE(hand.speed.get_snapshot()->speeds == sample_speeds());
}

// 计数换 rpm 由调用方乘 kSpeedRpmPerCount=0.732：250 计数 ≈ 183 rpm。
TEST_CASE("comm: speed counts convert to rpm", "[comm][l30][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(l30_comm::filled_i16(250)));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = hand.speed.get_blocking(kTimeout);
    for (const auto counts : data.speeds) {
        REQUIRE(counts == 250);
        REQUIRE(static_cast<float>(counts) * kSpeedRpmPerCount == Catch::Approx(183.0F).margin(0.01));
    }
}

// timeout<=0 立刻抛 invalid_argument，不发帧。
TEST_CASE("comm: speed get rejects non-positive timeout", "[comm][l30][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(l30_comm::filled_i16(10)));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.speed.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.get_blocking(std::chrono::milliseconds{-5}), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// 速度周期上报（0x00806008，无状态字节）不发请求也应刷新 snapshot。
TEST_CASE("comm: speed periodic report updates snapshot", "[comm][l30][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto frame = l30_comm::periodic_i16_frame(kSubSpeed, sample_counts());
    REQUIRE(frame.id == kReportId);
    raw->inject(frame);

    REQUIRE(wait_until([&hand] { return hand.speed.get_snapshot().has_value(); }));
    REQUIRE(hand.speed.get_snapshot()->speeds == sample_speeds());
    REQUIRE(l30_comm::frames_for(raw, kQueryId).empty());
}

// 非零状态字节 → ProtocolError，status() 带回原因码。
TEST_CASE("comm: speed read surfaces device status", "[comm][l30][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(l30_comm::filled_i16(0), 0x11));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    uint8_t observed = 0xFFU;
    std::string message;
    try {
        (void)hand.speed.get_blocking(kTimeout);
    } catch (const ProtocolError &error) {
        observed = error.status();
        message = error.what();
    }
    REQUIRE(observed == 0x11U);
    REQUIRE(message.find("parameter out of range") != std::string::npos);
}
