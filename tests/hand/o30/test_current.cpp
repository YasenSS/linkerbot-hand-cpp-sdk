#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <stdexcept>

#include "hand/o30/o30_fixture.hpp"

using namespace linkerbot::hand::o30;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
} // namespace

// 真机电流。MI 0x04，20 路 0–255。读 kActual 是当前电流；写会改运行限幅，
// 真机用例只读不写。

// 当前电流应有 20 个值。
TEST_CASE("Current: get_currents returns 20 actual values", "[o30][sensor][current]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.current.get_currents(kTimeout);
    REQUIRE(data.currents.size() == kJointCount);
}

// RTS 设定值也能读到 20 路。
TEST_CASE("Current: get_current_limits returns 20 values", "[o30][sensor][current]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.current.get_current_limits(kTimeout);
    REQUIRE(data.current_limits.size() == kJointCount);
}

// timeout<=0 该立刻抛，不落到总线上。
TEST_CASE("Current: rejects non-positive timeout", "[o30][sensor][current]") {
    auto &hand = test_util::o30_hand();
    REQUIRE_THROWS_AS(hand.current.get_currents(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

// get_currents 之后，管理器和整机快照里 current 都应有值。
TEST_CASE("Current: snapshot populated after read", "[o30][sensor][current]") {
    auto &hand = test_util::o30_hand();
    static_cast<void>(hand.current.get_currents(kTimeout));
    REQUIRE(hand.current.get_snapshot().has_value());
    REQUIRE(hand.get_snapshot().current.has_value());
}
