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

// FakeTransport 温度：0x33 关节 1–5，0x34 关节 6–10；raw 字节直接变 ℃。

// get 连发 0x33、0x34；填充 40 读回十个 40℃。
TEST_CASE("comm: temperature get_blocking reads 0x33 and 0x34", "[comm][l20lite][temperature]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x33, 5, 0x34, 5, 40));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.temperature.get_blocking(kTimeout);
    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x33});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x34});
    for (auto v : data.temperature.to_array()) {
        REQUIRE(v == Catch::Approx(40.f));
    }
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: temperature snapshot populates after get", "[comm][l20lite][temperature]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId,
                          l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x33, 5, 0x34, 5, 40));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.temperature.get_snapshot().has_value());
    (void)hand.temperature.get_blocking(kTimeout);
    REQUIRE(hand.temperature.get_snapshot().has_value());
}
