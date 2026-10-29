#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l6/l6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l6::Angle;
using linkerbot::hand::l6::L6;
using linkerbot::hand::l6::Side;

namespace {
    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kAngleCmd = 0x01;
    constexpr uint8_t kThumbCmd = 0xB1;
    constexpr auto kTimeout = std::chrono::milliseconds{100};

    Message angle_reply(uint8_t fill) {
        Message reply;
        reply.id = kLeftId;
        reply.dlc = 1 + static_cast<uint8_t>(Angle::kJointCount);
        reply.data[0] = kAngleCmd;
        for (size_t index = 0; index < Angle::kJointCount; ++index) {
            reply.data[index + 1] = fill;
        }
        return reply;
    }

    std::vector<Message> tactile_frames_with_invalid_row() {
        std::vector<Message> frames;
        frames.reserve(12);
        for (uint8_t row = 0; row < 12; ++row) {
            Message reply;
            reply.id = kLeftId;
            reply.dlc = 8;
            reply.data[0] = kThumbCmd;
            reply.data[1] = static_cast<uint8_t>(row << 4);
            for (uint8_t column = 0; column < 6; ++column) {
                reply.data[column + 2] = row;
            }
            if (row == 5) {
                reply.is_fd = true;
            }
            frames.push_back(reply);
        }
        return frames;
    }

    Message tactile_frame(uint8_t row, uint8_t fill) {
        Message reply;
        reply.id = kLeftId;
        reply.dlc = 8;
        reply.data[0] = kThumbCmd;
        reply.data[1] = row;
        for (uint8_t column = 0; column < 6; ++column) {
            reply.data[column + 2] = fill;
        }
        return reply;
    }
} // namespace

// 角度事务必须忽略扩展帧、FD 帧和错误长度，只接受精确的经典 CAN 回复。
TEST_CASE("comm: L6 angle accepts only an exact classic CAN reply", "[comm][l6][transaction]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, [](const Message &request) {
        if (request.dlc != 1 || request.data[0] != kAngleCmd) {
            return std::vector<Message>{};
        }

        auto extended = angle_reply(0x11);
        extended.is_extended = true;
        auto fd = angle_reply(0x22);
        fd.is_fd = true;
        auto oversized = angle_reply(0x33);
        oversized.dlc = 8;
        auto valid = angle_reply(0x44);
        return std::vector<Message>{extended, fd, oversized, valid};
    });

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto values = hand.angle.get_blocking(kTimeout).angle.to_array();
    for (const auto value : values) {
        REQUIRE(value > 26.0F);
        REQUIRE(value < 27.0F);
    }
}

// 单指触觉缺少任意一行精确经典帧时必须超时，不能把 FD 帧混入矩阵。
TEST_CASE("comm: L6 tactile rejects a non-classic row", "[comm][l6][transaction]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, [](const Message &request) {
        if (request.dlc != 2 || request.data[0] != kThumbCmd || request.data[1] != 0xC6) {
            return std::vector<Message>{};
        }
        return tactile_frames_with_invalid_row();
    });

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.force_sensor.get_finger("thumb").get_blocking(kTimeout), std::runtime_error);
}

// 触觉行号同时接受直接 0..11 编码和高半字节编码，并允许乱序到达。
TEST_CASE("comm: L6 tactile accepts direct row markers out of order", "[comm][l6][transaction]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, [](const Message &request) {
        if (request.dlc != 2 || request.data[0] != kThumbCmd) {
            return std::vector<Message>{};
        }
        std::vector<Message> frames;
        for (const uint8_t row : {uint8_t{5}, uint8_t{0}, uint8_t{11}, uint8_t{3}, uint8_t{1}, uint8_t{9}, uint8_t{2},
                                  uint8_t{8}, uint8_t{4}, uint8_t{7}, uint8_t{6}, uint8_t{10}}) {
            frames.push_back(tactile_frame(row, row));
        }
        return frames;
    });

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    const auto sample = hand.force_sensor.get_finger("thumb").get_blocking(kTimeout);
    for (uint8_t row = 0; row < 12; ++row) {
        REQUIRE(sample.values[row][0] == row);
    }
}

// 同一事务内相同行号内容不一致必须立刻报协议冲突，而不是静默覆盖。
TEST_CASE("comm: L6 tactile rejects a conflicting duplicate row", "[comm][l6][transaction]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, [](const Message &request) {
        if (request.dlc != 2 || request.data[0] != kThumbCmd) {
            return std::vector<Message>{};
        }
        return std::vector<Message>{tactile_frame(0, 0x10), tactile_frame(1, 0x11), tactile_frame(1, 0x22)};
    });

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    REQUIRE_THROWS_AS(hand.force_sensor.get_finger("thumb").get_blocking(kTimeout), std::runtime_error);
}

// 完整角度和全手触觉样本各自使用严格递增的 sequence，调用方不必按时间戳猜更新。
TEST_CASE("comm: L6 complete samples carry monotonic sequences", "[comm][l6][transaction]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, [](const Message &request) {
        if (request.dlc == 1 && request.data[0] == kAngleCmd) {
            return std::vector<Message>{angle_reply(0x44)};
        }
        if (request.dlc == 2 && request.data[0] >= 0xB1 && request.data[0] <= 0xB5 && request.data[1] == 0xC6) {
            auto frames = tactile_frames_with_invalid_row();
            for (auto &frame : frames) {
                frame.data[0] = request.data[0];
                frame.is_fd = false;
            }
            return frames;
        }
        return std::vector<Message>{};
    });

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto first_angle = hand.angle.get_blocking(kTimeout);
    const auto second_angle = hand.angle.get_blocking(kTimeout);
    REQUIRE(first_angle.sequence > 0);
    REQUIRE(second_angle.sequence == first_angle.sequence + 1);

    const auto first_force = hand.force_sensor.get_blocking(kTimeout);
    const auto second_force = hand.force_sensor.get_blocking(kTimeout);
    REQUIRE(first_force.sequence > 0);
    REQUIRE(second_force.sequence == first_force.sequence + 1);
    REQUIRE(first_force.thumb.sequence > 0);
    REQUIRE(first_force.pinky.sequence > 0);
}

// R15 回归：未知 reply 模式下 set_angles 发送失败必须立即清理 in-flight
// 事务，不能让后续指令在陈旧的事务占位上排队等待。
TEST_CASE("comm: L6 set_angles send failure clears in-flight state", "[comm][l6][transaction]") {
    auto owned = std::make_unique<FakeTransport>();
    auto *transport = owned.get();

    L6 hand(Side::kLeft, std::move(owned), "can0", test_util::unique_mapping_path());

    // Default-constructed AngleManager has no reply_enabled_ opinion yet, so
    // set_angles takes the "unknown reply mode" branch under test.
    std::array<uint8_t, Angle::kJointCount> raw{};
    raw.fill(0x10);

    transport->fail_writes("simulated write failure");
    REQUIRE_THROWS(hand.angle.set_angles(raw));
    REQUIRE(hand.angle.in_flight_count() == 0);
}
