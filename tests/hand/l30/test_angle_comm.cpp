#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/angle.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::Angle;
using linkerbot::hand::l30::kJointCount;
using linkerbot::hand::l30::kJointSpecs;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::protocol::kEmptyRequestBytes;
using linkerbot::hand::l30::protocol::kSubPosition;
using linkerbot::hand::l30::protocol::kVectorWireBytes;
using linkerbot::hand::l30::protocol::ProtocolError;

namespace {
    constexpr auto kTimeout = l30_comm::kTimeout;

    // 位置写命令 0x1/0x01/W 与位置查询 0x5/0x01/R 的真实仲裁 ID。
    constexpr uint32_t kWriteId = 0x02202100U;
    constexpr uint32_t kQueryId = 0x00A02100U;
    constexpr uint32_t kReportId = 0x00802008U;

    // 注入帧要经派发线程才会落到 snapshot，轮询等条件成立。
    template <typename Ready> bool wait_until(Ready ready) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (ready()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return false;
    }

    std::array<float, kJointCount> all_percents(float value) {
        std::array<float, kJointCount> values{};
        values.fill(value);
        return values;
    }

    // 0 %：屈伸轴落在最大原始值，其余轴落在最小原始值。
    std::vector<uint8_t> zero_percent_pose_bytes() {
        return {0x22, 0x00, 0x03, 0x84, 0x04, 0xB0, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x38,
                0x05, 0xDC, 0x06, 0x40, 0x06, 0x40, 0x05, 0xDC, 0x06, 0x40, 0x05, 0xDC,
                0xFF, 0x38, 0xFF, 0x38, 0xFF, 0x38, 0x06, 0x40, 0x05, 0xDC, 0xFC, 0x18};
    }

    // 每个关节都取自身 spec 里的一个合法值，含两个负数。
    std::array<int16_t, kJointCount> sample_raw() {
        return {0, 1200, 900, 800, -200, 1500, 1600, 1600, 1500, 1600, 1500, -200, 200, 0, 1600, 1500, -1000};
    }

    std::vector<uint8_t> sample_raw_bytes() {
        return {0x22, 0x00, 0x00, 0x00, 0x04, 0xB0, 0x03, 0x84, 0x03, 0x20, 0xFF, 0x38,
                0x05, 0xDC, 0x06, 0x40, 0x06, 0x40, 0x05, 0xDC, 0x06, 0x40, 0x05, 0xDC,
                0xFF, 0x38, 0x00, 0xC8, 0x00, 0x00, 0x06, 0x40, 0x05, 0xDC, 0xFC, 0x18};
    }
} // namespace

// FakeTransport 角度：写 0x02202100 发 34 字节大端 i16 向量，读 0x00A02100 空载荷；
// 命令值做范围校验，传感器读回按 from_sensor_raw 故意不校验。
// 每个用例都用 auto_start_periodic=false 构造，否则构造函数会阻塞等周期上报 ACK。

// set_angles_percent(Angle) 全 0 % → 一帧 0x02202100，22 00 + 17 个大端 i16，DLC 48。
TEST_CASE("comm: angle set_angles encodes one vector frame", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    hand.angle.set_angles_percent(Angle::from_array(all_percents(0.0F)));

    const auto frames = l30_comm::frames_for(raw, kWriteId);
    REQUIRE(frames.size() == 1);
    REQUIRE(l30_comm::useful(frames.front()) == zero_percent_pose_bytes());
    REQUIRE(frames.front().dlc == kVectorWireBytes);
    REQUIRE(frames.front().is_extended);
    REQUIRE(frames.front().is_fd);
}

// set_angles_percent(array) 与 Angle 重载同帧同字节。
TEST_CASE("comm: angle set_angles array overload matches", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    hand.angle.set_angles_percent(all_percents(0.0F));

    const auto frames = l30_comm::frames_for(raw, kWriteId);
    REQUIRE(frames.size() == 1);
    REQUIRE(l30_comm::useful(frames.front()) == zero_percent_pose_bytes());
    REQUIRE(frames.front().dlc == kVectorWireBytes);
}

// set_angles 直发协议整数：负数按二进制补码大端编码（-200→FF38，-1000→FC18）。
TEST_CASE("comm: angle set_angles encodes twos complement", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    hand.angle.set_angles(sample_raw());

    const auto frames = l30_comm::frames_for(raw, kWriteId);
    REQUIRE(frames.size() == 1);
    REQUIRE(l30_comm::useful(frames.front()) == sample_raw_bytes());
    REQUIRE(frames.front().dlc == kVectorWireBytes);
}

// 百分比 -1、101、NaN 都该抛 invalid_argument，且一个字节也不许上线。
TEST_CASE("comm: angle rejects out of range percent", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    auto low = all_percents(50.0F);
    low[0] = -1.0F;
    auto high = all_percents(50.0F);
    high[7] = 101.0F;
    auto nan = all_percents(50.0F);
    nan[16] = std::numeric_limits<float>::quiet_NaN();

    REQUIRE_THROWS_AS(hand.angle.set_angles_percent(low), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.angle.set_angles_percent(high), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.angle.set_angles_percent(nan), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// 原始命令按各关节 spec 校验：拇指根屈曲上限 900，901 与侧摆 -201 都该被拒。
TEST_CASE("comm: angle rejects raw beyond joint spec", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    auto over = sample_raw();
    over[0] = static_cast<int16_t>(kJointSpecs[0].maximum + 1);
    auto under = sample_raw();
    under[4] = static_cast<int16_t>(kJointSpecs[4].minimum - 1);

    REQUIRE_THROWS_AS(hand.angle.set_angles(over), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.angle.set_angles(under), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// get_blocking 发空载荷查询帧，解出百分比并刷新 snapshot。
TEST_CASE("comm: angle get_blocking decodes reply", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(sample_raw()));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_FALSE(hand.angle.get_snapshot().has_value());
    const auto data = hand.angle.get_blocking(kTimeout);

    const auto frames = l30_comm::frames_for(raw, kQueryId);
    REQUIRE(frames.size() == 1);
    REQUIRE(l30_comm::trimmed(frames.front()) == std::vector<uint8_t>{0x00, 0x00});
    REQUIRE(frames.front().dlc == kEmptyRequestBytes);
    REQUIRE(data.angles.to_raw() == sample_raw());
    REQUIRE(data.angles[16] == Catch::Approx(0.0F).margin(0.01));
    REQUIRE(hand.angle.get_snapshot().has_value());
    REQUIRE(hand.angle.get_snapshot()->angles.to_raw() == sample_raw());
}

// timeout<=0 立刻抛 invalid_argument，不发帧。
TEST_CASE("comm: angle get rejects non-positive timeout", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(l30_comm::filled_i16(0)));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.angle.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.angle.get_blocking(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// from_sensor_raw 不校验：真机会冲出命令行程，读回超限值不许抛。
TEST_CASE("comm: angle readback tolerates out of range raw", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    auto values = l30_comm::filled_i16(0);
    values[0] = static_cast<int16_t>(kJointSpecs[0].maximum + 300);
    values[16] = static_cast<int16_t>(kJointSpecs[16].minimum - 300);
    raw->on_request(kQueryId, l30_comm::i16_handler(values));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_NOTHROW(hand.angle.get_blocking(kTimeout));
    const auto data = hand.angle.get_blocking(kTimeout);
    REQUIRE(data.angles[0] < 0.0F);
    REQUIRE(data.angles[16] < 0.0F);
}

// 周期上报无状态字节、数据从 BYTE2 起，未发任何请求也应刷新 snapshot。
TEST_CASE("comm: angle periodic report updates snapshot", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    raw->inject(l30_comm::periodic_i16_frame(kSubPosition, sample_raw()));

    REQUIRE(wait_until([&hand] { return hand.angle.get_snapshot().has_value(); }));
    REQUIRE(hand.angle.get_snapshot()->angles.to_raw() == sample_raw());
    REQUIRE(l30_comm::frames_for(raw, kQueryId).empty());
}

// 位置上报 ID 应为 0x00802008。
TEST_CASE("comm: angle report id is 0x00802008", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto frame = l30_comm::periodic_i16_frame(kSubPosition, sample_raw());
    REQUIRE(frame.id == kReportId);
    raw->inject(frame);
    REQUIRE(wait_until([&hand] { return hand.angle.get_snapshot().has_value(); }));
}

// 非零状态字节 → ProtocolError，且 status() 带回设备给的原因码。
TEST_CASE("comm: angle read surfaces device status", "[comm][l30][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(l30_comm::filled_i16(0), 0x22));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    uint8_t observed = 0xFFU;
    std::string message;
    try {
        (void)hand.angle.get_blocking(kTimeout);
    } catch (const ProtocolError &error) {
        observed = error.status();
        message = error.what();
    }
    REQUIRE(observed == 0x22U);
    REQUIRE(message.find("motor not enabled") != std::string::npos);
}

// from_array 校验、百分比↔原始整数往返一致，from_sensor_raw 永不校验。
TEST_CASE("comm: angle percent and raw round trip", "[comm][l30][angle]") {
    const auto angles = Angle::from_array(all_percents(50.0F));
    const auto raw = angles.to_raw();
    for (size_t i = 0; i < kJointCount; ++i) {
        const auto span = static_cast<float>(kJointSpecs[i].maximum) - static_cast<float>(kJointSpecs[i].minimum);
        const auto expected = static_cast<float>(kJointSpecs[i].minimum) + (span / 2.0F);
        REQUIRE(static_cast<float>(raw[i]) == Catch::Approx(expected).margin(1.0));
    }

    const auto restored = Angle::from_sensor_raw(raw);
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(restored[i] == Catch::Approx(50.0F).margin(0.2));
    }

    auto bad = all_percents(50.0F);
    bad[3] = 100.5F;
    REQUIRE_THROWS_AS(Angle::from_array(bad), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(Angle::from_sensor_raw(l30_comm::filled_i16(30000)));
}
