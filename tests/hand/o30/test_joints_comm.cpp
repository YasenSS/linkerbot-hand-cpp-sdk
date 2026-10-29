#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include <linkerbot/hand/o30/angle.hpp>
#include <linkerbot/hand/o30/joints.hpp>

using linkerbot::hand::o30::Angle;
using linkerbot::hand::o30::joint_name;
using linkerbot::hand::o30::joint_percent_to_raw;
using linkerbot::hand::o30::joint_raw_to_percent;
using linkerbot::hand::o30::joint_slot;
using linkerbot::hand::o30::joint_sparse_id;
using linkerbot::hand::o30::kJointCount;
using linkerbot::hand::o30::kJointNames;
using linkerbot::hand::o30::kJointSlots;
using linkerbot::hand::o30::kJointSparseIds;
using linkerbot::hand::o30::kSafeWriteSegments;
using linkerbot::hand::o30::kSlotCount;
using linkerbot::hand::o30::percent_to_raw;
using linkerbot::hand::o30::percents_to_raw;
using linkerbot::hand::o30::project_u8;
using linkerbot::hand::o30::raw_to_percent;
using linkerbot::hand::o30::raw_to_percents;
using linkerbot::hand::o30::slot_to_joint;
using linkerbot::hand::o30::validate_percents;

namespace {
    std::array<float, kJointCount> filled_percent(float value) {
        std::array<float, kJointCount> values{};
        values.fill(value);
        return values;
    }

    template <typename Fn> std::string invalid_argument_message(Fn action) {
        try {
            action();
        } catch (const linkerbot::hand::ValidationError &error) {
            return error.what();
        }
        return {};
    }
} // namespace

// O30 关节表：20 个装配槽、稀疏 ID，以及按屈曲/侧摆区分的方向换算。

// kJointSlots / 名字 / 稀疏 ID 钉死物理布局，与协议和 Python SDK 一致。
TEST_CASE("comm: joint table matches the physical O30 layout", "[comm][o30][joints]") {
    REQUIRE(kJointCount == 20);
    REQUIRE(kSlotCount == 36);
    REQUIRE(kJointSlots.size() == 20);
    REQUIRE(kJointNames.size() == 20);
    REQUIRE(kJointSparseIds.size() == 20);

    const std::array<uint8_t, kJointCount> slots{0,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                                                 14, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    const std::array<const char *, kJointCount> names{
        "thumb_rotation", "thumb_roll",   "index_abd",  "middle_abd",  "ring_abd",    "pinky_abd",    "thumb_root1",
        "index_root1",    "middle_root1", "ring_root1", "pinky_root1", "index_root2", "middle_root2", "ring_root2",
        "pinky_root2",    "thumb_tip",    "index_tip",  "middle_tip",  "ring_tip",    "pinky_tip"};
    const std::array<uint8_t, kJointCount> sparse{0,  1, 6,  11, 16, 21, 2, 7,  12, 17,
                                                  22, 8, 13, 18, 23, 4,  9, 14, 19, 24};

    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(kJointSlots[i] == slots[i]);
        REQUIRE(kJointNames[i] == names[i]);
        REQUIRE(kJointSparseIds[i] == sparse[i]);
        REQUIRE(joint_slot(i) == slots[i]);
        REQUIRE(joint_name(i) == names[i]);
        REQUIRE(joint_sparse_id(i) == sparse[i]);
        REQUIRE(slot_to_joint(slots[i]) == i);
    }
    REQUIRE(slot_to_joint(1) == kSlotCount);
    REQUIRE(slot_to_joint(0x0F) == kSlotCount);
    REQUIRE_THROWS_AS(joint_name(kJointCount), std::out_of_range);
}

// 全量写入拆三段，跳过未装配槽 0x01-0x04、0x0F、0x19-0x23。
TEST_CASE("comm: safe write segments skip unimplemented slots", "[comm][o30][joints]") {
    REQUIRE(kSafeWriteSegments.size() == 3);
    REQUIRE(kSafeWriteSegments[0].si == 0x00);
    REQUIRE(kSafeWriteSegments[0].edl == 1);
    REQUIRE(kSafeWriteSegments[1].si == 0x05);
    REQUIRE(kSafeWriteSegments[1].edl == 10);
    REQUIRE(kSafeWriteSegments[2].si == 0x10);
    REQUIRE(kSafeWriteSegments[2].edl == 9);
}

// 36 槽投影只留下 20 个装配关节。
TEST_CASE("comm: project_u8 keeps only mounted slots", "[comm][o30][joints]") {
    std::array<uint8_t, kSlotCount> slots{};
    for (size_t i = 0; i < slots.size(); ++i) {
        slots[i] = static_cast<uint8_t>(i);
    }
    const auto joints = project_u8(slots);
    REQUIRE(joints[0] == 0);
    REQUIRE(joints[1] == 5);
    REQUIRE(joints[10] == 14);
    REQUIRE(joints[11] == 16);
    REQUIRE(joints[19] == 24);
}

// 屈伸轴：0% raw FF，100% raw 00；中间值检查量化舍入。
TEST_CASE("comm: flexion percent decreases from raw FF to zero", "[comm][o30][joints]") {
    REQUIRE(percent_to_raw(0.0F) == 255);
    REQUIRE(percent_to_raw(5.0F) == 242);
    REQUIRE(percent_to_raw(50.0F) == 128);
    REQUIRE(percent_to_raw(100.0F) == 0);

    REQUIRE(raw_to_percent(0) == Catch::Approx(100.0F));
    REQUIRE(raw_to_percent(255) == Catch::Approx(0.0F));
    REQUIRE(raw_to_percent(128) == Catch::Approx(127.0F * 100.0F / 255.0F));

    const auto raw = percents_to_raw(filled_percent(5.0F));
    REQUIRE(raw == std::array<uint8_t, kJointCount>{13,  13,  13,  13,  13,  13,  242, 242, 242, 242,
                                                    242, 242, 242, 242, 242, 242, 242, 242, 242, 242});
}

TEST_CASE("comm: indexed angle conversion follows the measured endpoints on every axis", "[comm][o30][joints]") {
    for (size_t i = 0; i < 20; ++i) {
        INFO("joint=" << joint_name(i));
        const uint8_t zero = i < 6 ? 0 : 255;
        const uint8_t hundred = i < 6 ? 255 : 0;
        REQUIRE(joint_percent_to_raw(i, 0.0F) == zero);
        REQUIRE(joint_percent_to_raw(i, 100.0F) == hundred);
        REQUIRE(joint_raw_to_percent(i, zero) == Catch::Approx(0.0F));
        REQUIRE(joint_raw_to_percent(i, hundred) == Catch::Approx(100.0F));
    }
    REQUIRE_THROWS_AS(joint_percent_to_raw(20, 50.0F), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(joint_raw_to_percent(20, 128), linkerbot::hand::ValidationError);
}

// 使用独立的 raw 常量验证编码与解码，而不是仅断言两函数互为逆函数。
TEST_CASE("comm: Angle percent and raw conversion", "[comm][o30][joints]") {
    const auto open = Angle::from_array(filled_percent(0.0F));
    const std::array<uint8_t, 20> zero_percent_raw{0,   0,   0,   0,   0,   0,   255, 255, 255, 255,
                                                   255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    REQUIRE(open.to_raw() == zero_percent_raw);
    REQUIRE(Angle::from_sensor_raw(zero_percent_raw).values == filled_percent(0.0F));

    std::array<uint8_t, 20> midpoint{};
    midpoint.fill(128);
    const auto from_raw = Angle::from_sensor_raw(midpoint);
    REQUIRE(from_raw[0] == Catch::Approx(50.19608F));
    REQUIRE(from_raw[6] == Catch::Approx(49.80392F));

    auto high = filled_percent(50.0F);
    high[3] = 101.0F;
    REQUIRE_THROWS_AS(Angle::from_array(high), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(Angle::from_sensor_raw(std::array<uint8_t, kJointCount>{}));
}

// 每个 raw 字节经百分比再转回应保持原值（浮点四舍五入）。
TEST_CASE("comm: raw percent round trip preserves every byte", "[comm][o30][joints]") {
    for (int raw = 0; raw < 256; ++raw) {
        std::array<uint8_t, kJointCount> values{};
        values.fill(static_cast<uint8_t>(raw));
        const auto back = percents_to_raw(raw_to_percents(values));
        REQUIRE(back == values);
    }
}

// validate_percents 接受 0 与 100，拒越界 / NaN / Inf，文案带下标。
TEST_CASE("comm: validate_percents rejects out of range values", "[comm][o30][joints]") {
    REQUIRE_NOTHROW(validate_percents(filled_percent(0.0F), "angles"));
    REQUIRE_NOTHROW(validate_percents(filled_percent(100.0F), "angles"));

    auto low = filled_percent(50.0F);
    low[7] = -0.1F;
    REQUIRE_THROWS_AS(validate_percents(low, "angles"), linkerbot::hand::ValidationError);

    auto high = filled_percent(50.0F);
    high[7] = 100.1F;
    const auto message = invalid_argument_message([&] { validate_percents(high, "angles"); });
    REQUIRE(message.find("angles") != std::string::npos);
    REQUIRE(message.find("[7]") != std::string::npos);
    REQUIRE(message.find("between 0 and 100") != std::string::npos);

    auto nan = filled_percent(50.0F);
    nan[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS_AS(validate_percents(nan, "angles"), linkerbot::hand::ValidationError);
    auto inf = filled_percent(50.0F);
    inf[0] = std::numeric_limits<float>::infinity();
    REQUIRE_THROWS_AS(validate_percents(inf, "angles"), linkerbot::hand::ValidationError);
}
