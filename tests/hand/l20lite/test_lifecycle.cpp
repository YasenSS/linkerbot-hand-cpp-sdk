#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/l20lite_fixture.hpp"

using namespace linkerbot::hand::l20lite;

// 真机生命周期。不共用 l20lite_hand()，每次自己 new，专门测构造 / 析构 / 重复 stop。

// 按环境变量 L20LITE_SIDE / CAN_INTERFACE 构造后，应能读到合法角度 [0, 100]。
TEST_CASE("Lifecycle: constructor creates usable instance", "[l20lite][lifecycle]") {
    L20lite hand(test_util::get_l20lite_side(), test_util::get_interface());
    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.angle.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 100.0f);
    }
}

// 离开作用域时析构会 stop；不能抛异常或卡死。
TEST_CASE("Lifecycle: destructor calls stop cleanly", "[l20lite][lifecycle]") {
    {
        L20lite hand(test_util::get_l20lite_side(), test_util::get_interface());
        (void)hand.angle.get_blocking(std::chrono::milliseconds{500});
    }
    REQUIRE(true);
}

// stop() 调两次都应成功（第二次是空操作）。
TEST_CASE("Lifecycle: stop is idempotent", "[l20lite][lifecycle]") {
    L20lite hand(test_util::get_l20lite_side(), test_util::get_interface());
    REQUIRE_NOTHROW(hand.stop());
    REQUIRE_NOTHROW(hand.stop());
}
