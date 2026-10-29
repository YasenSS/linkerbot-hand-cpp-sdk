#include <catch2/catch_test_macros.hpp>

#include "hand/l20/l20_fixture.hpp"

using namespace linkerbot::hand::l20;

// 真机故障码。0x59–0x5D 十六路 L20FaultCode 位域；clear_faults() 发清故障请求，无回包。
// 健康手应为 kNone。本文件不人为制造故障。

// 健康手十六个关节都应是 kNone，且 has_any_fault() 为 false。
TEST_CASE("Fault: no fault on healthy device", "[l20][sensor][fault]") {
    auto &hand = test_util::l20_hand();
    auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    for (auto code : data.fault.to_array()) {
        REQUIRE(code == L20FaultCode::kNone);
        REQUIRE_FALSE(has_fault(code));
    }
    REQUIRE_FALSE(data.fault.has_any_fault());
}

// get_blocking 之后，整机快照里 fault 应有值。
TEST_CASE("Fault: snapshot populated after read", "[l20][sensor][fault]") {
    auto &hand = test_util::l20_hand();
    (void)hand.fault.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().fault.has_value());
}

// get_blocking 能读回 16 个关节的故障码（只检查长度，不看具体值）。
TEST_CASE("Fault: get_blocking returns valid data", "[l20][sensor][fault]") {
    auto &hand = test_util::l20_hand();
    auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(data.fault.to_array().size() == Fault::kJointCount);
}

// clear_faults() 五指各发清故障帧，不应抛错（协议无回包，只检查发送成功）。
TEST_CASE("Fault: clear_faults no error", "[l20][sensor][fault]") {
    auto &hand = test_util::l20_hand();
    REQUIRE_NOTHROW(hand.fault.clear_faults());
}
