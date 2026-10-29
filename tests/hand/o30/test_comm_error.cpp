#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <stdexcept>

#include "hand/o30/o30_fixture.hpp"

using namespace linkerbot::hand::o30;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
} // namespace

// 真机通信错误历史。MI 0x4F；已验证固件返回 15 或 16 字节，
// 不能把 CAN FD padding 当作多出来的历史项。

// 最新下标应能单独读到。
TEST_CASE("CommError: get_latest_index is readable", "[o30][sensor][comm_error]") {
    auto &hand = test_util::o30_hand();
    static_cast<void>(hand.comm_error.get_latest_index(kTimeout));
}

// 历史长度必须是固件真实的 15 或 16；错误环在连续读取间可能变化。
TEST_CASE("CommError: history preserves the firmware length", "[o30][sensor][comm_error]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.comm_error.get_history(kTimeout);
    REQUIRE((data.history.size() == 15 || data.history.size() == 16));
}

// timeout<=0 该立刻抛，不落到总线上。
TEST_CASE("CommError: rejects non-positive timeout", "[o30][sensor][comm_error]") {
    auto &hand = test_util::o30_hand();
    REQUIRE_THROWS_AS(hand.comm_error.get_latest_index(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.comm_error.get_history(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}
