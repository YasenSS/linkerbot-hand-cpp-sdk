#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/o6/o6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::o6::O6;
using linkerbot::hand::o6::Side;
using linkerbot::hand::o6::Speed;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kSpeedCmd = 0x05;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    std::vector<uint8_t> last_payload(FakeTransport *transport) {
        const auto sent = transport->sent();
        REQUIRE_FALSE(sent.empty());
        const auto &msg = sent.back();
        return {msg.data.begin(), msg.data.begin() + msg.dlc};
    }

} // namespace

// FakeTransport 速度：0x05 读写，0–100% ↔ 0–255。

TEST_CASE("comm: speed 0x05 encodes percent to raw", "[comm][o6][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->echo_cmd(kLeftId, kSpeedCmd, Speed::kJointCount);

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.speed.set_speeds(Speed(100, 0, 50, 50, 50, 50));
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kSpeedCmd, 255, 0, 127, 127, 127, 127});
}

TEST_CASE("comm: speed get_blocking reads 0x05", "[comm][o6][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->echo_cmd(kLeftId, kSpeedCmd, Speed::kJointCount, 0x80);

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.speed.get_blocking(kTimeout);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kSpeedCmd});
    const auto arr = data.speed.to_array();
    for (size_t i = 0; i < Speed::kJointCount; ++i) {
        REQUIRE(arr[i] == Catch::Approx(128.0f * 100.0f / 255.0f));
    }
}

TEST_CASE("comm: speed snapshot populates after get", "[comm][o6][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->echo_cmd(kLeftId, kSpeedCmd, Speed::kJointCount, 0x40);

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.speed.get_snapshot().has_value());
    (void)hand.speed.get_blocking(kTimeout);
    REQUIRE(hand.speed.get_snapshot().has_value());
}
