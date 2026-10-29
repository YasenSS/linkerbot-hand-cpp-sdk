#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::Side;
using linkerbot::hand::l20lite::StallLimit;
using linkerbot::hand::l20lite::StallTime;

namespace {

    constexpr uint8_t kThresholdCmd = 0xC5;
    constexpr uint8_t kTimeCmd = 0xC6;
    constexpr uint8_t kTorqueCmd = 0xC7;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    FakeTransport::Handler stall_handler(uint32_t id, uint8_t fill) {
        return [id, fill](const Message &req) {
            if (req.dlc < 1) {
                return std::vector<Message>{};
            }
            const uint8_t cmd = req.data[0];
            if (cmd != kTimeCmd && cmd != kThresholdCmd && cmd != kTorqueCmd) {
                return std::vector<Message>{};
            }
            if (req.dlc >= 7) {
                Message reply;
                reply.id = id;
                reply.dlc = 7;
                for (uint8_t i = 0; i < 7; ++i) {
                    reply.data[i] = req.data[i];
                }
                return std::vector<Message>{reply};
            }

            std::vector<Message> replies;
            for (uint8_t bank = 1; bank <= 2; ++bank) {
                Message reply;
                reply.id = id;
                reply.dlc = 7;
                reply.data[0] = cmd;
                reply.data[1] = bank;
                for (size_t i = 0; i < 5; ++i) {
                    reply.data[i + 2] = fill;
                }
                replies.push_back(reply);
            }
            return replies;
        };
    }

} // namespace

// FakeTransport 堵转：BYTE1=1|2 分银行；0xC5/0xC7 为 0–100%；0xC6 为 ms/10。

// 500 ms → 两银行各 50；get 仅读取完整写响应缓存。
TEST_CASE("comm: stall time 0xC6 encodes milliseconds as raw/10", "[comm][l20lite][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, stall_handler(l20lite_comm::kLeftId, 50));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.stall.set_times(StallTime(500, 500, 500, 500, 500, 500, 500, 500, 500, 500));
    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{kTimeCmd, 1, 50, 50, 50, 50, 50});
    REQUIRE(sent.back() == std::vector<uint8_t>{kTimeCmd, 2, 50, 50, 50, 50, 50});

    const auto data = hand.stall.get_times_blocking(kTimeout);
    const auto after_get = l20lite_comm::payloads(raw);
    REQUIRE(after_get == sent);
    REQUIRE(data.timestamp == hand.stall.get_time_snapshot()->timestamp);
    REQUIRE(data.timestamp == hand.stall.get_times_blocking(kTimeout).timestamp);
    for (auto v : data.time.to_array()) {
        REQUIRE(v == Catch::Approx(500.0f));
    }
}

// 50% → 0x80；get 读回约 50%。
TEST_CASE("comm: stall threshold 0xC5 uses 0-100 percent scale", "[comm][l20lite][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, stall_handler(l20lite_comm::kLeftId, 0x80));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.stall.set_thresholds(StallLimit(50, 50, 50, 50, 50, 50, 50, 50, 50, 50));
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{kThresholdCmd, 2, 128, 128, 128, 128, 128});

    const auto data = hand.stall.get_thresholds_blocking(kTimeout);
    for (auto v : data.limit.to_array()) {
        REQUIRE(v == Catch::Approx(128.0f * 100.0f / 255.0f));
    }
}

// 100% → 0xFF（银行 2 最后一帧）。
TEST_CASE("comm: stall torque 0xC7 max 100 maps to 0xFF", "[comm][l20lite][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, stall_handler(l20lite_comm::kLeftId, 0xFF));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.stall.set_torques(StallLimit(100, 100, 100, 100, 100, 100, 100, 100, 100, 100));
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{kTorqueCmd, 2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
}

// get 前未 set 应拒绝：固件没有单字节空读。
TEST_CASE("comm: stall get without prior set is rejected", "[comm][l20lite][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId, stall_handler(l20lite_comm::kLeftId, 50));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.stall.get_times_blocking(kTimeout), std::runtime_error);
}

// set 完整响应后 snapshot 有值，无需 get。
TEST_CASE("comm: stall snapshots populate after complete write response", "[comm][l20lite][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId, stall_handler(l20lite_comm::kLeftId, 50));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.stall.get_time_snapshot().has_value());
    hand.stall.set_times(StallTime(500, 500, 500, 500, 500, 500, 500, 500, 500, 500));
    REQUIRE(hand.stall.get_time_snapshot().has_value());
}

// 时间 >2550、阈值 >100、NaN 都应拒绝。
TEST_CASE("comm: stall rejects out-of-range values", "[comm][l20lite][stall]") {
    REQUIRE_THROWS_AS(StallTime(3000, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(StallLimit(101, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS_AS(StallTime(nan, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
}

// timeout<=0 应立刻抛。
TEST_CASE("comm: stall get rejects non-positive timeout", "[comm][l20lite][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, stall_handler(l20lite_comm::kLeftId, 50));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.stall.get_times_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

TEST_CASE("comm: all stall getters are cache-only and retain response timestamps", "[comm][l20lite][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, [](const Message &req) {
        if (req.dlc != 7 || req.data[0] < kThresholdCmd || req.data[0] > kTorqueCmd) {
            return std::vector<Message>{};
        }
        auto reply = req;
        for (size_t i = 2; i < 7; ++i) {
            reply.data[i] = req.data[1] == 1 ? 25 : 75;
        }
        return std::vector<Message>{reply};
    });
    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    const auto before = raw->sent().size();
    REQUIRE_THROWS_AS(hand.stall.get_times_blocking(), linkerbot::hand::DeviceNotReadyError);
    REQUIRE_THROWS_AS(hand.stall.get_thresholds_blocking(), linkerbot::hand::DeviceNotReadyError);
    REQUIRE_THROWS_AS(hand.stall.get_torques_blocking(), linkerbot::hand::DeviceNotReadyError);
    REQUIRE(raw->sent().size() == before);
    std::array<float, 10> times{}, limits{};
    times.fill(500);
    limits.fill(50);
    hand.stall.set_times(times, kTimeout);
    hand.stall.set_thresholds(limits, kTimeout);
    hand.stall.set_torques(limits, kTimeout);
    const auto written = raw->sent().size();
    const auto time = hand.stall.get_times_blocking();
    const auto threshold = hand.stall.get_thresholds_blocking();
    const auto torque = hand.stall.get_torques_blocking();
    REQUIRE(time.time.to_array()[0] == 250);
    REQUIRE(time.time.to_array()[5] == 750);
    REQUIRE(threshold.limit.to_array()[0] == Catch::Approx(25.0f * 100 / 255));
    REQUIRE(torque.limit.to_array()[5] == Catch::Approx(75.0f * 100 / 255));
    REQUIRE(time.timestamp == hand.stall.get_times_blocking().timestamp);
    REQUIRE(threshold.timestamp == hand.stall.get_thresholds_blocking().timestamp);
    REQUIRE(torque.timestamp == hand.stall.get_torques_blocking().timestamp);
    REQUIRE(raw->sent().size() == written);
}

TEST_CASE("comm: incomplete stall writes never publish partial or requested values", "[comm][l20lite][stall]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, stall_handler(l20lite_comm::kLeftId, 50));
    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    std::array<float, 10> values{};
    values.fill(500);
    SECTION("no earlier response stays unavailable") {}
    SECTION("last complete response survives failed write") { hand.stall.set_times(values); }
    const auto previous = hand.stall.get_time_snapshot();
    raw->on_request(l20lite_comm::kLeftId, [](const Message &req) {
        if (req.dlc == 7 && req.data[0] == kTimeCmd && req.data[1] == 1) {
            return std::vector<Message>{req};
        }
        return std::vector<Message>{};
    });
    values.fill(800);
    REQUIRE_THROWS_AS(hand.stall.set_times(values, std::chrono::milliseconds{30}), linkerbot::hand::TimeoutError);
    const auto after = hand.stall.get_time_snapshot();
    REQUIRE(after.has_value() == previous.has_value());
    const auto sent = raw->sent().size();
    if (previous) {
        REQUIRE(after->time.to_array() == previous->time.to_array());
        REQUIRE(after->timestamp == previous->timestamp);
        REQUIRE(hand.stall.get_times_blocking().timestamp == previous->timestamp);
    } else {
        REQUIRE_THROWS_AS(hand.stall.get_times_blocking(), linkerbot::hand::DeviceNotReadyError);
    }
    REQUIRE(raw->sent().size() == sent);
}
