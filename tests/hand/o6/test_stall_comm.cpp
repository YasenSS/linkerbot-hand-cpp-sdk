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
#include <linkerbot/hand/o6/o6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::o6::O6;
using linkerbot::hand::o6::Side;
using linkerbot::hand::o6::StallLimit;
using linkerbot::hand::o6::StallTime;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kThresholdCmd = 0xC5;
    constexpr uint8_t kTimeCmd = 0xC6;
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

// FakeTransport 堵转：新协议 0xC5 阈值、0xC6 时间 ms/10、0xC7 扭矩；mA = raw*1000/255。

// 500 ms → 线上 50；get 发单字节 0xC6，读回 500。
TEST_CASE("comm: stall time 0xC6 encodes milliseconds as raw/10", "[comm][o6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 50));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

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
TEST_CASE("comm: stall time encodes per-joint milliseconds", "[comm][o6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 0));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.stall.set_times(StallTime(100, 200, 300, 400, 500, 600));
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kTimeCmd, 10, 20, 30, 40, 50, 60});
}

// 500 mA → 0x80；get 发单字节 0xC5，读回约 500。
TEST_CASE("comm: stall threshold 0xC5 uses 0-1000 mA scale", "[comm][o6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 0x80));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    // 500 * 255 / 1000 = 127.5 -> 128 = 0x80
    hand.stall.set_thresholds(StallLimit(500, 500, 500, 500, 500, 500));
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kThresholdCmd, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80});

    const auto data = hand.stall.get_thresholds_blocking(kTimeout);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kThresholdCmd});
    const auto arr = data.limit.to_array();
    for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
        REQUIRE(arr[i] == Catch::Approx(500.0f).margin(8.0f));
    }
}

// 1000 mA → 0xFF；get 发单字节 0xC7。
TEST_CASE("comm: stall torque 0xC7 max 1000 maps to 0xFF", "[comm][o6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 0xFF));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.stall.set_torques(StallLimit(1000, 1000, 1000, 1000, 1000, 1000));
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kTorqueCmd, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});

    const auto data = hand.stall.get_torques_blocking(kTimeout);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kTorqueCmd});
    const auto arr = data.limit.to_array();
    for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
        REQUIRE(arr[i] == Catch::Approx(1000.0f).margin(8.0f));
    }
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: stall snapshots populate after get", "[comm][o6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 50));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.stall.get_time_snapshot().has_value());
    (void)hand.stall.get_times_blocking(kTimeout);
    REQUIRE(hand.stall.get_time_snapshot().has_value());
}

// timeout<=0 应立刻抛。
TEST_CASE("comm: stall get rejects non-positive timeout", "[comm][o6][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, stall_handler(kLeftId, 50));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.stall.get_times_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

// 时间 >2550、阈值 >1000、NaN、负数都应拒绝。
TEST_CASE("comm: stall rejects out-of-range values", "[comm][o6][stall]") {
    REQUIRE_THROWS_AS(StallTime(3000, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(StallLimit(1001, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(StallTime(-1, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(StallLimit(-1, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS_AS(StallTime(nan, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(StallLimit(nan, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
}
