#include "hand/support/optional_value.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <linkerbot/hand/o30/angle.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

#include "hand/o30/comm_helpers.hpp"
#include "hand/o30/o30_fixture.hpp"

using linkerbot::hand::o30::Angle;
using linkerbot::hand::o30::kJointCount;
using linkerbot::hand::o30::kTipCount;
using linkerbot::hand::o30::ValueKind;
using linkerbot::hand::o30::protocol::kMiPosition;
using linkerbot::hand::o30::protocol::kMiPosition16;
using linkerbot::hand::o30::protocol::kMiSparsePosition;

namespace {
    constexpr auto kTimeout = o30_comm::kTimeout;
} // namespace

// FakeTransport 角度：MI 0x01 三段写入、实际/设定值读取、稀疏 0x30、半字 0x20。

// set_angles 全 0x80：三帧 01 00 01 80 / 01 05 0A + 10×80 / 01 10 09 + 9×80。
TEST_CASE("comm: set_angles writes only mounted slots", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    h.hand->angle.set_angles(o30_comm::filled_u8(0x80), kTimeout);

    const auto sent = o30_comm::payloads(h.transport);
    REQUIRE(sent.size() == 3);
    REQUIRE(sent[0] == std::vector<uint8_t>({0x01, 0x00, 0x01, 0x80}));
    auto mid = std::vector<uint8_t>{0x01, 0x05, 0x0A};
    mid.insert(mid.end(), 10, 0x80);
    REQUIRE(sent[1] == mid);
    auto last = std::vector<uint8_t>{0x01, 0x10, 0x09};
    last.insert(last.end(), 9, 0x80);
    REQUIRE(sent[2] == last);
    for (const auto &msg : h.transport->sent()) {
        REQUIRE(msg.id == 0x001);
        REQUIRE_FALSE(msg.is_extended);
        REQUIRE(msg.is_fd);
        REQUIRE_FALSE(msg.is_brs);
    }
}

// 5%：非屈伸轴为 0D，屈伸轴为 F2。
TEST_CASE("comm: set_angles applies the direction of each joint", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    h.hand->angle.set_angles_percent(o30_comm::filled_percent(5.0F), kTimeout);

    REQUIRE(h.hop->writes.size() == 3);
    REQUIRE(h.hop->writes[0].payload == std::vector<uint8_t>({0x0D}));
    REQUIRE(h.hop->writes[1].payload ==
            std::vector<uint8_t>{0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2});
    REQUIRE(h.hop->writes[2].payload == std::vector<uint8_t>(9, 0xF2));
}

// 整手两种重载和三个分组接口都必须把张开/握拳编成相同的屈曲字节。
TEST_CASE("comm: all percentage angle setters use the corrected flexion endpoints", "[comm][o30][angle][direction]") {
    const auto percent = GENERATE(0.0F, 100.0F);
    const auto api = GENERATE(0, 1, 2, 3, 4);
    INFO("api=" << api << ", percent=" << percent);
    const uint8_t flexion = percent == 0.0F ? 0xFF : 0x00;
    const uint8_t transverse = percent == 0.0F ? 0x00 : 0xFF;
    auto h = o30_comm::make_hand();
    h.hop->put(kMiPosition, std::vector<uint8_t>(36, 0xA5));
    auto expected = std::vector<uint8_t>(36, 0xA5);
    if (api <= 1) {
        const auto target = o30_comm::filled_percent(percent);
        if (api == 0) {
            h.hand->angle.set_angles_percent(target, kTimeout);
        } else {
            h.hand->angle.set_angles_percent(Angle::from_array(target), kTimeout);
        }
        expected[0] = transverse;
        for (size_t slot = 5; slot < 10; ++slot) {
            expected[slot] = transverse;
        }
        for (size_t slot = 10; slot < 15; ++slot) {
            expected[slot] = flexion;
        }
        for (size_t slot = 16; slot < 25; ++slot) {
            expected[slot] = flexion;
        }
        REQUIRE(h.hop->writes.size() == 3);
    } else {
        std::array<float, 5> five{};
        five.fill(percent);
        std::array<float, 4> four{};
        four.fill(percent);
        size_t begin = 0;
        size_t count = 0;
        if (api == 2) {
            h.hand->angle.set_tips(five, kTimeout);
            begin = 20;
            count = 5;
        }
        if (api == 3) {
            h.hand->angle.set_roots1(five, kTimeout);
            begin = 10;
            count = 5;
        }
        if (api == 4) {
            h.hand->angle.set_roots2(four, kTimeout);
            begin = 16;
            count = 4;
        }
        for (size_t slot = begin; slot < begin + count; ++slot) {
            expected[slot] = flexion;
        }
        REQUIRE(h.hop->writes.size() == 1);
    }
    REQUIRE(h.hop->objects.at({kMiPosition, false}) == expected);
}

// 实际值与设定值分别注入原始字节，防止同一错误在写入/读回两端相互抵消。
TEST_CASE("comm: angle readback and snapshots decode all measured joint endpoints", "[comm][o30][angle][direction]") {
    const auto kind = GENERATE(ValueKind::kActual, ValueKind::kSetpoint);
    auto h = o30_comm::make_hand();
    h.hop->put(kMiPosition, std::vector<uint8_t>(36, 0x00), false);
    h.hop->put(kMiPosition, std::vector<uint8_t>(36, 0xFF), true);
    const auto data = h.hand->angle.get_blocking(kTimeout, kind);
    REQUIRE(data.kind == kind);
    const auto manager = h.hand->angle.get_snapshot();
    const auto hand = h.hand->get_snapshot();
    REQUIRE(manager.has_value());
    REQUIRE(hand.angle.has_value());
    for (size_t i = 0; i < 20; ++i) {
        const auto actual_percent = i < 6 ? 0.0F : 100.0F;
        const auto expected = kind == ValueKind::kActual ? actual_percent : 100.0F - actual_percent;
        REQUIRE(data.angles.values[i] == expected);
        REQUIRE(test_support::require_value(manager).angles.values[i] == expected);
        REQUIRE(test_support::require_value(hand.angle).angles.values[i] == expected);
    }
}

TEST_CASE("comm: open and closed fixtures preserve yaw and roll targets", "[comm][o30][angle][direction]") {
    std::array<uint8_t, 20> original{100, 10, 207, 189, 123, 91, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const auto reference = Angle::from_sensor_raw(original);
    const auto opening = test_util::o30_open_pose(reference);
    const auto closing = test_util::o30_closed_pose(reference);
    auto h = o30_comm::make_hand();
    h.hand->angle.set_angles_percent(opening, kTimeout);
    const auto open_slots = h.hop->objects.at({kMiPosition, false});
    h.hand->angle.set_angles_percent(closing, kTimeout);
    const auto closed_slots = h.hop->objects.at({kMiPosition, false});
    constexpr std::array<uint8_t, 20> slots{0, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    for (size_t i = 0; i < 20; ++i) {
        REQUIRE(open_slots[slots[i]] == (i < 6 ? original[i] : 0x00));
        REQUIRE(closed_slots[slots[i]] == (i < 6 ? original[i] : 0xB3));
    }
}

// 修复百分比方向不得改变 raw 或稀疏接口的协议字节。
TEST_CASE("comm: raw angle setters retain protocol bytes at both endpoints", "[comm][o30][angle][direction]") {
    const auto raw = GENERATE(uint8_t{0x00}, uint8_t{0xFF});
    const auto api = GENERATE(0, 1, 2, 3, 4, 5);
    INFO("api=" << api << ", raw=" << static_cast<unsigned>(raw));
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(0x41, 0x61, 8, "0.0.4");
    std::array<uint8_t, 5> five{};
    five.fill(raw);
    std::array<uint8_t, 4> four{};
    four.fill(raw);
    switch (api) {
    case 0:
        h.hand->angle.set_angles(o30_comm::filled_u8(raw), kTimeout);
        break;
    case 1:
        h.hand->angle.set_raw_tips(five, kTimeout);
        break;
    case 2:
        h.hand->angle.set_raw_roots1(five, kTimeout);
        break;
    case 3:
        h.hand->angle.set_raw_roots2(four, kTimeout);
        break;
    case 4:
        h.hand->angle.set_sparse_joints({{16, raw}}, kTimeout);
        break;
    case 5:
        h.hand->angle.set_sparse({{9, raw}}, kTimeout);
        break;
    }
    REQUIRE(h.hop->writes.size() == (api == 0 ? 3 : 1));
    size_t bytes = 0;
    for (const auto &write : h.hop->writes) {
        REQUIRE(write.mi == 0x01);
        for (const auto value : write.payload) {
            REQUIRE(value == raw);
            ++bytes;
        }
    }
    constexpr std::array<size_t, 6> expected_counts{20, 5, 5, 4, 1, 1};
    REQUIRE(bytes == expected_counts[api]);
}

// 固定拇指横滚与四指侧摆的物理 SI，避免用 SDK 的同一张映射表生成期望值。
TEST_CASE("comm: O30 full angle writes isolate thumb roll and each abduction joint", "[comm][o30][angle][yaw]") {
    const auto finger = GENERATE(size_t{0}, size_t{1}, size_t{2}, size_t{3}, size_t{4});
    const auto percent = GENERATE(25.0F, 75.0F);
    constexpr std::array<uint8_t, 5> yaw_slots{0x05, 0x06, 0x07, 0x08, 0x09};
    const uint8_t raw = percent == 25.0F ? 0x40 : 0xBF;
    INFO("finger=" << finger << ", percent=" << percent);

    auto h = o30_comm::make_hand();
    const auto original = o30_comm::ramp_slots();
    h.hop->put(kMiPosition, original);
    auto target = linkerbot::hand::o30::raw_to_percents(o30_comm::projected_ramp());
    target[1 + finger] = percent;
    h.hand->angle.set_angles_percent(target, kTimeout);

    auto expected = original;
    expected[yaw_slots[finger]] = raw;
    REQUIRE(h.hop->objects.at({kMiPosition, false}) == expected);
    REQUIRE(h.hop->writes.size() == 3);
    const auto actual = h.hand->angle.get_blocking(kTimeout);
    REQUIRE(actual.angles.values[1 + finger] == Catch::Approx(percent).margin(0.2F));
}

// HOP 0.0.4 的拇指横滚与四指侧摆分别兼容关节索引和手指序稀疏 ID；只写一个 MI 0x01 槽。
TEST_CASE("comm: O30 0.0.4 sparse writes isolate thumb roll and four abduction joints",
          "[comm][o30][angle][yaw][v004]") {
    const auto finger = GENERATE(size_t{0}, size_t{1}, size_t{2}, size_t{3}, size_t{4});
    const auto use_sparse_id = GENERATE(false, true);
    const auto raw = GENERATE(uint8_t{0xBF}, uint8_t{0x40});
    constexpr std::array<uint8_t, 5> yaw_slots{0x05, 0x06, 0x07, 0x08, 0x09};
    constexpr std::array<uint8_t, 5> yaw_sparse_ids{1, 6, 11, 16, 21};
    INFO("finger=" << finger << ", use_sparse_id=" << use_sparse_id << ", raw=" << static_cast<unsigned>(raw));

    auto h = o30_comm::make_hand();
    h.hop->put_ascii(0x41, 0x61, 8, "0.0.4");
    const auto original = o30_comm::ramp_slots();
    h.hop->put(kMiPosition, original);
    if (use_sparse_id) {
        h.hand->angle.set_sparse({{yaw_sparse_ids[finger], raw}}, kTimeout);
    } else {
        h.hand->angle.set_sparse_joints({{1 + finger, raw}}, kTimeout);
    }

    auto expected = original;
    expected[yaw_slots[finger]] = raw;
    REQUIRE(h.hop->objects.at({kMiPosition, false}) == expected);
    REQUIRE(h.hop->writes.size() == 1);
    REQUIRE(h.hop->writes[0].mi == 0x01);
    REQUIRE(h.hop->writes[0].si == yaw_slots[finger]);
    REQUIRE(h.hop->writes[0].payload == std::vector<uint8_t>{raw});
}

// 百分比越界 / NaN 不许上总线。
TEST_CASE("comm: set_angles rejects out of range percent", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    auto low = o30_comm::filled_percent(50.0F);
    low[0] = -1.0F;
    auto high = o30_comm::filled_percent(50.0F);
    high[7] = 101.0F;
    auto nan = o30_comm::filled_percent(50.0F);
    nan[16] = std::numeric_limits<float>::quiet_NaN();

    REQUIRE_THROWS_AS(h.hand->angle.set_angles_percent(low), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->angle.set_angles_percent(high), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->angle.set_angles_percent(nan), linkerbot::hand::ValidationError);
    REQUIRE(h.transport->sent().empty());
}

// get_blocking 读 01 00 24，投影 20 关节并刷新 snapshot。
TEST_CASE("comm: get_blocking projects 36 slots onto 20 joints", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    h.hop->put(kMiPosition, o30_comm::ramp_slots());

    REQUIRE_FALSE(h.hand->angle.get_snapshot().has_value());
    const auto data = h.hand->angle.get_blocking(kTimeout);

    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({0x01, 0x00, 0x24}));
    REQUIRE(data.kind == ValueKind::kActual);
    REQUIRE(data.angles.to_raw() == o30_comm::projected_ramp());
    REQUIRE(h.hand->angle.get_snapshot().has_value());
    REQUIRE(test_support::require_value(h.hand->angle.get_snapshot()).angles.to_raw() == o30_comm::projected_ramp());
}

// 设定值读 CTRL=0x81。
TEST_CASE("comm: get_blocking setpoint uses RTS", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    auto target = o30_comm::ramp_slots();
    for (auto &byte : target) {
        byte = static_cast<uint8_t>(255U - byte);
    }
    h.hop->put(kMiPosition, o30_comm::ramp_slots(), false);
    h.hop->put(kMiPosition, target, true);

    const auto actual = h.hand->angle.get_blocking(kTimeout, ValueKind::kActual);
    const auto setpoint = h.hand->angle.get_blocking(kTimeout, ValueKind::kSetpoint);

    const auto sent = o30_comm::payloads(h.transport);
    REQUIRE(sent[0] == std::vector<uint8_t>({0x01, 0x00, 0x24}));
    REQUIRE(sent[1] == std::vector<uint8_t>({0x81, 0x00, 0x24}));
    REQUIRE(actual.angles.to_raw() == o30_comm::projected_ramp());
    REQUIRE(setpoint.kind == ValueKind::kSetpoint);
}

// get_slots 返回完整 36 槽，含未装配位置。
TEST_CASE("comm: get_slots returns the full 36-byte object", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    h.hop->put(kMiPosition, o30_comm::ramp_slots());
    const auto slots = h.hand->angle.get_slots(kTimeout);
    REQUIRE(slots.size() == 36);
    REQUIRE(slots[1] == 1);
    REQUIRE(slots[15] == 15);
}

// 指尖写 SI=0x14 EDL=5。
TEST_CASE("comm: set_raw_tips writes SI 0x14", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    std::array<uint8_t, kTipCount> tips{0x11, 0x22, 0x33, 0x44, 0x55};
    h.hand->angle.set_raw_tips(tips, kTimeout);

    REQUIRE(o30_comm::last_payload(h.transport) ==
            std::vector<uint8_t>({0x01, 0x14, 0x05, 0x11, 0x22, 0x33, 0x44, 0x55}));
}

// 稀疏 0x30：关节 15/16 是拇指/食指尖，线号 0x04 / 0x09。
TEST_CASE("comm: set_sparse_joints uses finger-major wire IDs", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    h.hand->angle.set_sparse_joints({{15, 0x80}, {16, 0x80}}, kTimeout);

    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({0x30, 0x00, 0x04, 0x04, 0x80, 0x09, 0x80}));
}

// set_sparse 直接发手指序 ID；空列表与越界关节都拒。
TEST_CASE("comm: set_sparse validates pairs", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    h.hand->angle.set_sparse({{0x04, 0x80}, {0x09, 0x81}}, kTimeout);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({0x30, 0x00, 0x04, 0x04, 0x80, 0x09, 0x81}));

    REQUIRE_THROWS_AS(h.hand->angle.set_sparse({}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->angle.set_sparse({{36, 0}}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->angle.set_sparse_joints({{20, 0}}), linkerbot::hand::ValidationError);
}

// get_halfword 发 20 00 48，小端 72 字节投影 20 关节。
TEST_CASE("comm: get_halfword reads MI 0x20 little-endian", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    std::vector<uint8_t> wire(72, 0);
    auto put_i16 = [&](size_t slot, uint16_t value) {
        wire[slot * 2] = static_cast<uint8_t>(value & 0xFFU);
        wire[slot * 2 + 1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    };
    put_i16(0, 0x8000);
    put_i16(5, 0x0002);
    put_i16(24, 0x7FFF);
    h.hop->put(kMiPosition16, wire);

    const auto data = h.hand->angle.get_halfword(kTimeout);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({0x20, 0x00, 0x48}));
    REQUIRE(data.positions[0] == 0x8000);
    REQUIRE(data.positions[1] == 0x0002);
    REQUIRE(data.positions[19] == 0x7FFF);
}

// timeout<=0 立刻抛，不发帧。
TEST_CASE("comm: angle get rejects non-positive timeout", "[comm][o30][angle]") {
    auto h = o30_comm::make_hand();
    REQUIRE_THROWS_AS(h.hand->angle.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->angle.get_blocking(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE(h.transport->sent().empty());
}
