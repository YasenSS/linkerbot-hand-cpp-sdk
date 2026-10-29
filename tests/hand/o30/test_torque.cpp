#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>

#include "hand/o30/o30_fixture.hpp"

using namespace linkerbot::hand::o30;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
} // namespace

// 真机实际转矩。MI 0x06，20 路 0–255，空闲可为 0。

// 当前转矩应有 20 个值。
TEST_CASE("Torque: get_torques returns 20 actual values", "[o30][sensor][torque]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.torque.get_torques(kTimeout);
    REQUIRE(data.torques.size() == kJointCount);
}

// timeout<=0 该立刻抛，不落到总线上。
TEST_CASE("Torque: rejects non-positive timeout", "[o30][sensor][torque]") {
    auto &hand = test_util::o30_hand();
    REQUIRE_THROWS_AS(hand.torque.get_torques(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

// get_torques 之后，管理器和整机快照里 torque 都应有值。
TEST_CASE("Torque: snapshot populated after read", "[o30][sensor][torque]") {
    auto &hand = test_util::o30_hand();
    static_cast<void>(hand.torque.get_torques(kTimeout));
    REQUIRE(hand.torque.get_snapshot().has_value());
    REQUIRE(hand.get_snapshot().torque.has_value());
}
