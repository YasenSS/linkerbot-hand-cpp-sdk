#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20/l20.hpp>

#include "hand/l20/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l20::kJointCount;
using linkerbot::hand::l20::L20;
using linkerbot::hand::l20::Side;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
    constexpr uint8_t kCurrentCmd = 0x51;
    constexpr uint8_t kFill50 = 0x80;

    std::array<uint8_t, kJointCount> all_raw(uint8_t value) {
        std::array<uint8_t, kJointCount> values{};
        values.fill(value);
        return values;
    }
} // namespace

// FakeTransport 电流/电流限制：五帧 0x51–0x55。

// 全 50% 下发五帧 0x51–0x55；保留关节槽为 0x00。
TEST_CASE("comm: l20 current limit encodes raw values across five frames", "[comm][l20][current-limit]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kCurrentCmd, 0));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.current_limit.set_current_limits(all_raw(128));
    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    REQUIRE(sent[sent.size() - 5] == std::vector<uint8_t>{0x51, 128, 128, 128, 0, 0, 128});
    REQUIRE(sent[sent.size() - 4] == std::vector<uint8_t>{0x52, 128, 0, 128, 0, 0, 128});
    REQUIRE(sent[sent.size() - 3] == std::vector<uint8_t>{0x53, 128, 0, 128, 0, 0, 128});
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x54, 128, 0, 128, 0, 0, 128});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x55, 128, 0, 128, 0, 0, 128});
}

// get 连发 DLC=1 的 0x51..0x55；填充 0x80 读回约 50%。
TEST_CASE("comm: l20 current get_blocking reads 0x51 through 0x55", "[comm][l20][current]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kCurrentCmd, kFill50));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.current.get_blocking(kTimeout);
    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    REQUIRE(sent[sent.size() - 5] == std::vector<uint8_t>{0x51});
    REQUIRE(sent[sent.size() - 4] == std::vector<uint8_t>{0x52});
    REQUIRE(sent[sent.size() - 3] == std::vector<uint8_t>{0x53});
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x54});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x55});
    const auto joints = data.currents;
    REQUIRE(joints.size() == kJointCount);
    for (auto v : joints) {
        REQUIRE(v == 128);
    }
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: l20 current snapshot populates after get", "[comm][l20][current]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kCurrentCmd, 0x40));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.current.get_snapshot().has_value());
    (void)hand.current.get_blocking(kTimeout);
    REQUIRE(hand.current.get_snapshot().has_value());
}
