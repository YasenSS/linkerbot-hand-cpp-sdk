#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>

#include <linkerbot/hand/l30/l30.hpp>

#include "hand/l30/l30_fixture.hpp"

using namespace linkerbot::hand::l30;

// 真机生命周期。不共用 l30_hand()，每次自己开一次 CAN FD 接口，
// 专门测构造 / 析构 / 重复 stop。

// 按环境变量 L30_NODE_ID / CAN_INTERFACE 构造后，应能读到角度；
// 读回按 from_sensor_raw 不夹取，所以用放宽区间而不是命令行程 [0, 100]。
TEST_CASE("Lifecycle: constructor creates usable instance", "[l30][lifecycle]") {
    L30 hand(test_util::get_l30_interface(), test_util::get_l30_node_id());
    const auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    for (size_t i = 0; i < kJointCount; ++i) {
        INFO("joint[" << i << "]=" << std::string(joint_name(i)));
        REQUIRE(data.angles.values[i] >= test_util::kAngleReadLowerBound);
        REQUIRE(data.angles.values[i] <= test_util::kAngleReadUpperBound);
    }
}

// 离开作用域时析构会 stop；不能抛异常或卡死。
TEST_CASE("Lifecycle: destructor calls stop cleanly", "[l30][lifecycle]") {
    {
        L30 hand(test_util::get_l30_interface(), test_util::get_l30_node_id());
        static_cast<void>(hand.angle.get_blocking(std::chrono::milliseconds{500}));
    }
    REQUIRE(true);
}

// stop() 调两次都应成功（第二次是空操作）。
TEST_CASE("Lifecycle: stop is idempotent", "[l30][lifecycle]") {
    L30 hand(test_util::get_l30_interface(), test_util::get_l30_node_id());
    REQUIRE_NOTHROW(hand.stop());
    REQUIRE_NOTHROW(hand.stop());
}

// node_id() / host_id() 应回读构造时给的地址。
TEST_CASE("Lifecycle: addresses match the constructor", "[l30][lifecycle]") {
    L30 hand(test_util::get_l30_interface(), test_util::get_l30_node_id());
    REQUIRE(hand.node_id() == test_util::get_l30_node_id());
    REQUIRE(hand.host_id() == kDefaultHostId);
}
