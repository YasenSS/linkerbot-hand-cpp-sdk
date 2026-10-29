#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::Side;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
}

// FakeTransport 电流/电流限制：0x02 关节 1–5，0x03 关节 6–10。

// 100/0/50% → 0x02[255,0,128,…]、0x03[128,…,0] 两帧。
TEST_CASE("comm: current limit encodes raw values across two frames", "[comm][l20lite][current-limit]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x02, 5, 0x03, 5, 0));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.current_limit.set_current_limits({255, 0, 128, 128, 128, 128, 128, 128, 128, 0});
    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x02, 255, 0, 128, 128, 128});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x03, 128, 128, 128, 128, 0});
}

// get 连发 0x02、0x03；填充 0x80 读回约 50%。
TEST_CASE("comm: current get_blocking reads 0x02 and 0x03", "[comm][l20lite][current]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x02, 5, 0x03, 5, 0x80));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.current.get_blocking(kTimeout);
    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x02});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x03});
    for (auto v : data.currents) {
        REQUIRE(v == 128);
    }
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: current snapshot populates after get", "[comm][l20lite][current]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId,
                          l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x02, 5, 0x03, 5, 0x40));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.current.get_snapshot().has_value());
    (void)hand.current.get_blocking(kTimeout);
    REQUIRE(hand.current.get_snapshot().has_value());
}
