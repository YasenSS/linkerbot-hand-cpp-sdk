#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20/l20.hpp>

#include "hand/l20/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l20::Angle;
using linkerbot::hand::l20::L20;
using linkerbot::hand::l20::Side;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
    constexpr uint8_t kAngleCmd = 0x41;
    constexpr uint8_t kFill50 = 0x80;

    std::array<float, Angle::kJointCount> all_percent(float value) {
        std::array<float, Angle::kJointCount> values{};
        values.fill(value);
        return values;
    }
} // namespace

// FakeTransport 角度：五帧 0x41–0x45，DLC=7 写 / DLC=1 读；拇指 [abd,yaw,root1,0,0,tip]，
// 其余 [abd,0,root1,0,0,tip]；0–100% ↔ 0–255（50% → 128）。

// 全 50% 下发五帧；拇指 0x41 的 BYTE4/BYTE5（payload[3,4] → data[4], data[5]）为保留 0。
TEST_CASE("comm: l20 angle set_angles 50 percent sends five frames", "[comm][l20][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kAngleCmd, 0));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.angle.set_angles_percent(all_percent(50.0f));
    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    REQUIRE(sent[sent.size() - 5] == std::vector<uint8_t>{0x41, 128, 128, 128, 0, 0, 128});
    REQUIRE(sent[sent.size() - 4] == std::vector<uint8_t>{0x42, 128, 0, 128, 0, 0, 128});
    REQUIRE(sent[sent.size() - 3] == std::vector<uint8_t>{0x43, 128, 0, 128, 0, 0, 128});
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x44, 128, 0, 128, 0, 0, 128});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x45, 128, 0, 128, 0, 0, 128});
    REQUIRE(sent[sent.size() - 5][4] == 0);
    REQUIRE(sent[sent.size() - 5][5] == 0);
}

// get 连发 DLC=1 的 0x41..0x45；填充 0x80 组装 16 关节，读回约 50%。
TEST_CASE("comm: l20 angle get_blocking reads 0x41 through 0x45", "[comm][l20][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kAngleCmd, kFill50));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.angle.get_blocking(kTimeout);
    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    REQUIRE(sent[sent.size() - 5] == std::vector<uint8_t>{0x41});
    REQUIRE(sent[sent.size() - 4] == std::vector<uint8_t>{0x42});
    REQUIRE(sent[sent.size() - 3] == std::vector<uint8_t>{0x43});
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x44});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x45});
    const auto joints = data.angle.to_array();
    REQUIRE(joints.size() == Angle::kJointCount);
    for (auto v : joints) {
        REQUIRE(v == Catch::Approx(128.0f * 100.0f / 255.0f));
    }
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: l20 angle snapshot populates after get", "[comm][l20][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kAngleCmd, 0x40));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.angle.get_snapshot().has_value());
    (void)hand.angle.get_blocking(kTimeout);
    REQUIRE(hand.angle.get_snapshot().has_value());
}

// timeout<=0 应立刻抛。
TEST_CASE("comm: l20 angle get rejects non-positive timeout", "[comm][l20][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kAngleCmd, 0));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.angle.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}
