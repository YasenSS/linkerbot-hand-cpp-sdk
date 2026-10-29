#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l6/l6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l6::L6;
using linkerbot::hand::l6::Side;
using linkerbot::hand::l6::StallLimit;
using linkerbot::hand::l6::StallTime;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kTimeCmd = 0xC5;
    constexpr uint8_t kThresholdCmd = 0xC6;
    constexpr uint8_t kTorqueCmd = 0xC7;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    std::vector<uint8_t> last_payload(FakeTransport *transport) {
        const auto sent = transport->sent();
        REQUIRE_FALSE(sent.empty());
        const auto &msg = sent.back();
        return {msg.data.begin(), msg.data.begin() + msg.dlc};
    }

    FakeTransport::Handler stall_handler(uint32_t id, uint8_t fill) {
        return [id, fill](const Message &req) {
            if (req.dlc < 1) {
                return std::vector<Message>{};
            }
            const uint8_t cmd = req.data[0];
            if (cmd != kTimeCmd && cmd != kThresholdCmd && cmd != kTorqueCmd) {
                return std::vector<Message>{};
            }
            Message reply;
            reply.id = id;
            reply.dlc = 7;
            reply.data[0] = cmd;
            for (size_t i = 0; i < 6; ++i) {
                reply.data[i + 1] = (req.dlc >= 7) ? req.data[i + 1] : fill;
            }
            return std::vector<Message>{reply};
        };
    }

} // namespace

// FakeTransport 堵转：0xC5 时间 ms/10，0xC6/0xC7 按 0–1400 线性映射；get 发 1 字节。

// 500 ms → 线上 50；get 发单字节 0xC5，读回 500。
TEST_CASE("comm: stall time 0xC5 encodes milliseconds as raw/10", "[comm][l6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 50));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.stall.set_times(StallTime(500, 500, 500, 500, 500, 500));
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kTimeCmd, 50, 50, 50, 50, 50, 50});

    const auto data = hand.stall.get_times_blocking(kTimeout);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kTimeCmd});
    const auto arr = data.time.to_array();
    for (size_t i = 0; i < StallTime::kJointCount; ++i) {
        REQUIRE(arr[i] == Catch::Approx(500.0f));
    }
}

// 六关节时间各不相同：100..600 ms → 线上 10..60。
TEST_CASE("comm: stall time encodes per-joint milliseconds", "[comm][l6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 0));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.stall.set_times(StallTime(100, 200, 300, 400, 500, 600));
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kTimeCmd, 10, 20, 30, 40, 50, 60});
}

// set_times(array) 与结构体版编码相同。
TEST_CASE("comm: stall time accepts std::array overload", "[comm][l6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 50));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const std::array<float, 6> values = {500, 500, 500, 500, 500, 500};
    hand.stall.set_times(values);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kTimeCmd, 50, 50, 50, 50, 50, 50});
}

// 500 → 0x5B；get 发单字节 0xC6，读回约 500。
TEST_CASE("comm: stall threshold 0xC6 uses 0-1400 scale", "[comm][l6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 0x5B));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    // 500 * 255 / 1400 = 91.07 -> 91 = 0x5B
    hand.stall.set_thresholds(StallLimit(500, 500, 500, 500, 500, 500));
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kThresholdCmd, 0x5B, 0x5B, 0x5B, 0x5B, 0x5B, 0x5B});

    const auto data = hand.stall.get_thresholds_blocking(kTimeout);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kThresholdCmd});
    const auto arr = data.limit.to_array();
    for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
        REQUIRE(arr[i] == Catch::Approx(500.0f).margin(8.0f));
    }
}

// 700 → 0x80；get 发单字节 0xC7，读回约 700。
TEST_CASE("comm: stall torque 0xC7 default 700 maps to 0x80", "[comm][l6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 0x80));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    // 700 * 255 / 1400 = 127.5 -> 128 = 0x80
    hand.stall.set_torques(StallLimit(700, 700, 700, 700, 700, 700));
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kTorqueCmd, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80});

    const auto data = hand.stall.get_torques_blocking(kTimeout);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kTorqueCmd});
    const auto arr = data.limit.to_array();
    for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
        REQUIRE(arr[i] == Catch::Approx(700.0f).margin(8.0f));
    }
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: stall snapshots populate after get", "[comm][l6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 50));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.stall.get_time_snapshot().has_value());
    (void)hand.stall.get_times_blocking(kTimeout);
    REQUIRE(hand.stall.get_time_snapshot().has_value());
}

// timeout<=0 应立刻抛。
TEST_CASE("comm: stall get rejects non-positive timeout", "[comm][l6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 50));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.stall.get_times_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

// 时间 >2550、阈值 >1400、NaN、负数都应拒绝。
TEST_CASE("comm: stall rejects out-of-range values", "[comm][l6][stall]") {
    REQUIRE_THROWS_AS(StallTime(3000, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(StallLimit(1401, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(StallTime(-1, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(StallLimit(-1, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS_AS(StallTime(nan, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(StallLimit(nan, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
}

TEST_CASE("L6 native stall writes reject values quantized to raw zero but retain zero readback",
          "[comm][l6][stall][parity]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *transport = fake.get();
    fake->on_request(kLeftId, stall_handler(kLeftId, 0));
    L6 hand(Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    const auto before = transport->sent().size();
    for (const auto ineffective : {0.0F, 1.0F, 4.0F}) {
        auto values = std::array<float, 6>{10, 10, 10, 10, 10, 10};
        values[2] = ineffective;
        REQUIRE_THROWS_AS(hand.stall.set_times(values), linkerbot::hand::ValidationError);
        REQUIRE(transport->sent().size() == before);
    }
    REQUIRE(hand.stall.get_times_blocking(kTimeout).time.to_array() == std::array<float, 6>{0, 0, 0, 0, 0, 0});
    hand.stall.set_times(StallTime{10, 10, 10, 10, 10, 2550});
    REQUIRE(last_payload(transport) == std::vector<uint8_t>{0xC5, 1, 1, 1, 1, 1, 255});
}
