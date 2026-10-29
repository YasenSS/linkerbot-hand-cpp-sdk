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
using linkerbot::hand::l20::L20;
using linkerbot::hand::l20::Side;
using linkerbot::hand::l20::Speed;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
    constexpr uint8_t kSpeedCmd = 0x49;
    constexpr uint8_t kFill50 = 0x80;

    std::array<float, Speed::kJointCount> all_percent(float value) {
        std::array<float, Speed::kJointCount> values{};
        values.fill(value);
        return values;
    }
} // namespace

// FakeTransport 速度：五帧 0x49–0x4D；拇指 [abd,yaw,root1,0,0,tip]，其余 yaw 槽为 0；
// 0–100% ↔ 0–255（50% → 128）。

// 全 50% 下发五帧 0x49–0x4D；保留关节槽为 0x00。
TEST_CASE("comm: l20 speed encodes percent across five frames", "[comm][l20][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kSpeedCmd, 0));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.speed.set_speeds(all_percent(50.0f));
    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    REQUIRE(sent[sent.size() - 5] == std::vector<uint8_t>{0x49, 128, 128, 128, 0, 0, 128});
    REQUIRE(sent[sent.size() - 4] == std::vector<uint8_t>{0x4A, 128, 0, 128, 0, 0, 128});
    REQUIRE(sent[sent.size() - 3] == std::vector<uint8_t>{0x4B, 128, 0, 128, 0, 0, 128});
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x4C, 128, 0, 128, 0, 0, 128});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x4D, 128, 0, 128, 0, 0, 128});
}

// get 连发 DLC=1 的 0x49..0x4D；填充 0x80 读回约 50%。
TEST_CASE("comm: l20 speed get_blocking reads 0x49 through 0x4D", "[comm][l20][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kSpeedCmd, kFill50));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.speed.get_blocking(kTimeout);
    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    REQUIRE(sent[sent.size() - 5] == std::vector<uint8_t>{0x49});
    REQUIRE(sent[sent.size() - 4] == std::vector<uint8_t>{0x4A});
    REQUIRE(sent[sent.size() - 3] == std::vector<uint8_t>{0x4B});
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x4C});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x4D});
    const auto joints = data.speed.to_array();
    REQUIRE(joints.size() == Speed::kJointCount);
    for (auto v : joints) {
        REQUIRE(v == Catch::Approx(128.0f * 100.0f / 255.0f));
    }
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: l20 speed snapshot populates after get", "[comm][l20][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kSpeedCmd, 0x40));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.speed.get_snapshot().has_value());
    (void)hand.speed.get_blocking(kTimeout);
    REQUIRE(hand.speed.get_snapshot().has_value());
}

// timeout<=0 应立刻抛。
TEST_CASE("comm: l20 speed get rejects non-positive timeout", "[comm][l20][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kSpeedCmd, 0));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.speed.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}
