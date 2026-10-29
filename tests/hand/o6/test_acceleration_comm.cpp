#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/o6/o6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::o6::Acceleration;
using linkerbot::hand::o6::O6;
using linkerbot::hand::o6::Side;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kAccelCmd = 0x87;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    std::vector<uint8_t> last_payload(FakeTransport *transport) {
        const auto sent = transport->sent();
        REQUIRE_FALSE(sent.empty());
        const auto &msg = sent.back();
        return {msg.data.begin(), msg.data.begin() + msg.dlc};
    }

} // namespace

// FakeTransport 加速度：0x87；用户 100→线上 0（最大），0→1，99→254。

TEST_CASE("comm: acceleration 100 percent maps to hardware 0", "[comm][o6][acceleration]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->echo_cmd(kLeftId, kAccelCmd, Acceleration::kJointCount);

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.acceleration.set_accelerations(Acceleration(100, 100, 100, 100, 100, 100));
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kAccelCmd, 0, 0, 0, 0, 0, 0});
}

TEST_CASE("comm: acceleration 0 and 99 map to 1 and 254", "[comm][o6][acceleration]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->echo_cmd(kLeftId, kAccelCmd, Acceleration::kJointCount);

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.acceleration.set_accelerations(Acceleration(0, 99, 0, 99, 0, 99));
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kAccelCmd, 1, 254, 1, 254, 1, 254});
}

TEST_CASE("comm: acceleration get_blocking decodes hardware 0 as 100", "[comm][o6][acceleration]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->echo_cmd(kLeftId, kAccelCmd, Acceleration::kJointCount, 0);

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.acceleration.get_blocking(kTimeout);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kAccelCmd});
    const auto arr = data.acceleration.to_array();
    for (size_t i = 0; i < Acceleration::kJointCount; ++i) {
        REQUIRE(arr[i] == Catch::Approx(100.0f));
    }
}

TEST_CASE("comm: acceleration get_blocking decodes hardware 1 as 0", "[comm][o6][acceleration]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->echo_cmd(kLeftId, kAccelCmd, Acceleration::kJointCount, 1);

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto arr = hand.acceleration.get_blocking(kTimeout).acceleration.to_array();
    for (size_t i = 0; i < Acceleration::kJointCount; ++i) {
        REQUIRE(arr[i] == Catch::Approx(0.0f));
    }
}

TEST_CASE("comm: acceleration rejects out-of-range values", "[comm][o6][acceleration]") {
    REQUIRE_THROWS_AS(Acceleration(101, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Acceleration(-1, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS_AS(Acceleration(nan, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
}
