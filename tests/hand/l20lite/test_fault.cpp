#include <catch2/catch_test_macros.hpp>

#include "hand/l20lite/l20lite_fixture.hpp"

using namespace linkerbot::hand::l20lite;

// 真机故障码。0x35+0x36；新协议无 0x83 清故障。健康手应为 kNone。

// 健康手十个关节都应是 kNone，且 has_any_fault() 为 false。
TEST_CASE("Fault: no fault on healthy device", "[l20lite][sensor][fault]") {
    auto &hand = test_util::l20lite_hand();
    auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    for (auto code : data.fault.to_array()) {
        REQUIRE(code == FaultCode::kNone);
    }
    REQUIRE_FALSE(data.fault.has_any_fault());
}

// get_blocking 之后，整机快照里 fault 应有值。
TEST_CASE("Fault: snapshot populated after read", "[l20lite][sensor][fault]") {
    auto &hand = test_util::l20lite_hand();
    (void)hand.fault.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().fault.has_value());
}

// get_blocking 能读回 10 个关节的故障码（只检查长度，不看具体值）。
TEST_CASE("Fault: get_blocking returns valid data", "[l20lite][sensor][fault]") {
    auto &hand = test_util::l20lite_hand();
    auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(data.fault.to_array().size() == Fault::kJointCount);
}
