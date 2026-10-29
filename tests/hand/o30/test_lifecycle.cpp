#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>

#include <linkerbot/hand/o30/o30.hpp>

#include "hand/o30/o30_fixture.hpp"

using namespace linkerbot::hand::o30;

// 真机生命周期。不共用 o30_hand()，每次自己开一次 CAN FD 接口。

// 按 CAN_INTERFACE / O30_REQUEST_ID 构造后应能读到 20 个百分比。
TEST_CASE("Lifecycle: constructor creates usable instance", "[o30][lifecycle]") {
    O30 hand(test_util::get_o30_interface(), test_util::get_o30_request_id());
    const auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(data.angles.values.size() == kJointCount);
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(data.angles.values[i] >= 0.0F);
        REQUIRE(data.angles.values[i] <= 100.0F);
    }
}

// 离开作用域时析构会 stop；不能抛或卡死。
TEST_CASE("Lifecycle: destructor calls stop cleanly", "[o30][lifecycle]") {
    {
        O30 hand(test_util::get_o30_interface(), test_util::get_o30_request_id());
        static_cast<void>(hand.angle.get_blocking(std::chrono::milliseconds{500}));
    }
    REQUIRE(true);
}

// stop() 调两次都应成功。
TEST_CASE("Lifecycle: stop is idempotent", "[o30][lifecycle]") {
    O30 hand(test_util::get_o30_interface(), test_util::get_o30_request_id());
    REQUIRE_NOTHROW(hand.stop());
    REQUIRE_NOTHROW(hand.stop());
}

// 默认请求/应答 ID 应与实测标准帧一致，除非环境变量改了请求 ID。
TEST_CASE("Lifecycle: CAN IDs follow the constructor", "[o30][lifecycle]") {
    O30 hand(test_util::get_o30_interface(), test_util::get_o30_request_id());
    REQUIRE(hand.request_id() == test_util::get_o30_request_id());
    REQUIRE(hand.response_id() == hand.request_id() + 0x400U);
}
