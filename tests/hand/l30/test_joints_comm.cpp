#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include <linkerbot/hand/l30/joints.hpp>

using linkerbot::hand::l30::joint_name;
using linkerbot::hand::l30::JointSpec;
using linkerbot::hand::l30::kCurrentLimitMax;
using linkerbot::hand::l30::kCurrentLimitMin;
using linkerbot::hand::l30::kCurrentMilliampsPerCount;
using linkerbot::hand::l30::kJointCount;
using linkerbot::hand::l30::kJointSpecs;
using linkerbot::hand::l30::kSpeedMax;
using linkerbot::hand::l30::kSpeedMin;
using linkerbot::hand::l30::kSpeedRpmPerCount;
using linkerbot::hand::l30::percent_to_raw;
using linkerbot::hand::l30::percents_to_raw;
using linkerbot::hand::l30::raw_to_percent;
using linkerbot::hand::l30::raw_to_percents;
using linkerbot::hand::l30::validate_percents;
using linkerbot::hand::l30::validate_raw_commands;
using linkerbot::hand::l30::validate_u16_range;

namespace {
    // 中位对称的关节：四个侧摆加腕关节，50 % 应落在 raw 0。
    constexpr std::array<size_t, 5> kSymmetricJoints{4, 11, 12, 13, 16};

    std::array<float, kJointCount> filled_percents(float value) {
        std::array<float, kJointCount> values{};
        values.fill(value);
        return values;
    }

    std::array<uint16_t, kJointCount> filled_u16(uint16_t value) {
        std::array<uint16_t, kJointCount> values{};
        values.fill(value);
        return values;
    }

    // 每个关节都取自身量程中点，得到一组合法的 raw 命令。
    std::array<int16_t, kJointCount> midpoint_raw() {
        std::array<int16_t, kJointCount> raw{};
        for (size_t i = 0; i < kJointCount; ++i) {
            raw[i] = percent_to_raw(50.0F, kJointSpecs[i]);
        }
        return raw;
    }

    // 仓库没有 matcher，异常文案只能手动捕获后用 find 断言。
    template <typename Fn> std::string invalid_argument_message(Fn action) {
        try {
            action();
        } catch (const linkerbot::hand::ValidationError &error) {
            return error.what();
        }
        return {};
    }
} // namespace

// L30 joints 纯单元测试：钉住 17 个关节的名字与量程、百分比与 raw 的双向换算、
// 百分比/raw/u16 三套校验函数的边界与文案，以及电流与速度的刻度系数。

// kJointSpecs 的 17 条名字与上下限逐字钉死，任何改动都要显式改测试。
TEST_CASE("comm: kJointSpecs pins all 17 joint names and ranges", "[comm][l30][joints]") {
    REQUIRE(kJointCount == 17);
    REQUIRE(kJointSpecs.size() == kJointCount);

    const std::array<JointSpec, kJointCount> expected{{
        {"thumb_root", 0, 900},
        {"thumb_tip", 0, 1200},
        {"thumb_abd", 0, 900},
        {"thumb_rotation", 0, 800},
        {"ring_abd", -200, 200},
        {"ring_tip", 0, 1500},
        {"ring_root", 0, 1600},
        {"middle_root", 0, 1600},
        {"middle_tip", 0, 1500},
        {"pinky_root", 0, 1600},
        {"pinky_tip", 0, 1500},
        {"pinky_abd", -200, 200},
        {"middle_abd", -200, 200},
        {"index_abd", -200, 200},
        {"index_root", 0, 1600},
        {"index_tip", 0, 1500},
        {"wrist", -1000, 1000},
    }};

    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(kJointSpecs[i].name == expected[i].name);
        REQUIRE(kJointSpecs[i].minimum == expected[i].minimum);
        REQUIRE(kJointSpecs[i].maximum == expected[i].maximum);
    }
}

// joint_name 与 kJointSpecs 一致，越界索引由 std::array::at 抛 out_of_range。
TEST_CASE("comm: joint_name matches kJointSpecs", "[comm][l30][joints]") {
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(joint_name(i) == kJointSpecs[i].name);
    }
    REQUIRE(joint_name(0) == "thumb_root");
    REQUIRE(joint_name(16) == "wrist");
    REQUIRE_THROWS_AS(joint_name(kJointCount), std::out_of_range);
}

// 屈伸轴反向，其余轴递增；端点与 Python 统一接口一致。
TEST_CASE("comm: percent_to_raw maps the bounds onto the joint spec", "[comm][l30][joints]") {
    for (size_t i = 0; i < kJointCount; ++i) {
        const auto &spec = kJointSpecs[i];
        const auto reversed = i == 0 || i == 1 || (i >= 5 && i <= 10) || i == 14 || i == 15;
        const auto zero = reversed ? spec.maximum : spec.minimum;
        const auto hundred = reversed ? spec.minimum : spec.maximum;
        REQUIRE(percent_to_raw(0.0F, spec) == zero);
        REQUIRE(percent_to_raw(100.0F, spec) == hundred);
        REQUIRE(raw_to_percent(zero, spec) == Catch::Approx(0.0F));
        REQUIRE(raw_to_percent(hundred, spec) == Catch::Approx(100.0F));
    }
}

// 对称关节 50 % 落在 raw 0；单向弯曲关节 50 % 落在量程中点。
TEST_CASE("comm: 50 percent is raw zero on the symmetric joints", "[comm][l30][joints]") {
    for (const auto index : kSymmetricJoints) {
        const auto &spec = kJointSpecs[index];
        REQUIRE(spec.minimum == -spec.maximum);
        REQUIRE(percent_to_raw(50.0F, spec) == 0);
        REQUIRE(raw_to_percent(0, spec) == Catch::Approx(50.0F));
    }

    REQUIRE(percent_to_raw(50.0F, kJointSpecs[0]) == 450);
    REQUIRE(percent_to_raw(50.0F, kJointSpecs[1]) == 600);
    REQUIRE(percent_to_raw(50.0F, kJointSpecs[3]) == 400);
    REQUIRE(percent_to_raw(50.0F, kJointSpecs[6]) == 800);
}

// 四分位百分比在两个方向上都能原值往返。
TEST_CASE("comm: percent_to_raw and raw_to_percent round-trip", "[comm][l30][joints]") {
    for (size_t i = 0; i < kJointCount; ++i) {
        const auto &spec = kJointSpecs[i];
        for (const float percent : {0.0F, 25.0F, 50.0F, 75.0F, 100.0F}) {
            const auto raw = percent_to_raw(percent, spec);
            REQUIRE(raw >= spec.minimum);
            REQUIRE(raw <= spec.maximum);
            REQUIRE(raw_to_percent(raw, spec) == Catch::Approx(percent));
            REQUIRE(percent_to_raw(raw_to_percent(raw, spec), spec) == raw);
        }
    }
}

// 整组 17 路的 percents_to_raw / raw_to_percents 往返。
TEST_CASE("comm: percents_to_raw and raw_to_percents round-trip all joints", "[comm][l30][joints]") {
    std::array<float, kJointCount> percents{};
    for (size_t i = 0; i < kJointCount; ++i) {
        percents[i] = static_cast<float>(i % 5) * 25.0F;
    }

    const auto raw = percents_to_raw(percents);
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(raw[i] == percent_to_raw(percents[i], kJointSpecs[i]));
    }

    const auto back = raw_to_percents(raw);
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(back[i] == Catch::Approx(percents[i]));
    }
    REQUIRE(percents_to_raw(back) == raw);

    const auto bounds = percents_to_raw(filled_percents(100.0F));
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(bounds[i] == percent_to_raw(100.0F, kJointSpecs[i]));
    }
    const auto zeros = percents_to_raw(filled_percents(0.0F));
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(zeros[i] == percent_to_raw(0.0F, kJointSpecs[i]));
    }
}

// validate_percents 接受 0 与 100，拒 -0.1、100.1 与 NaN，且文案带 label 与下标。
TEST_CASE("comm: validate_percents accepts the bounds and rejects NaN", "[comm][l30][joints]") {
    REQUIRE_NOTHROW(validate_percents(filled_percents(0.0F), "angle"));
    REQUIRE_NOTHROW(validate_percents(filled_percents(100.0F), "angle"));
    REQUIRE_NOTHROW(validate_percents(filled_percents(50.0F), "angle"));

    REQUIRE_THROWS_AS(validate_percents(filled_percents(-0.1F), "angle"), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(validate_percents(filled_percents(100.1F), "angle"), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(validate_percents(filled_percents(std::numeric_limits<float>::quiet_NaN()), "angle"),
                      linkerbot::hand::ValidationError);

    auto values = filled_percents(50.0F);
    values[7] = 100.1F;
    const auto message = invalid_argument_message([&] { validate_percents(values, "angle"); });
    REQUIRE(message.find("angle") != std::string::npos);
    REQUIRE(message.find("[7]") != std::string::npos);
    REQUIRE(message.find("between 0 and 100") != std::string::npos);
}

// validate_raw_commands 接受各关节上下限，超出一档就抛，文案带关节名。
TEST_CASE("comm: validate_raw_commands enforces every joint spec", "[comm][l30][joints]") {
    std::array<int16_t, kJointCount> lows{};
    std::array<int16_t, kJointCount> highs{};
    for (size_t i = 0; i < kJointCount; ++i) {
        lows[i] = kJointSpecs[i].minimum;
        highs[i] = kJointSpecs[i].maximum;
    }
    REQUIRE_NOTHROW(validate_raw_commands(lows, "position"));
    REQUIRE_NOTHROW(validate_raw_commands(highs, "position"));
    REQUIRE_NOTHROW(validate_raw_commands(midpoint_raw(), "position"));

    auto over = midpoint_raw();
    over[6] = static_cast<int16_t>(kJointSpecs[6].maximum + 1);
    REQUIRE_THROWS_AS(validate_raw_commands(over, "position"), linkerbot::hand::ValidationError);
    const auto message = invalid_argument_message([&] { validate_raw_commands(over, "position"); });
    REQUIRE(message.find("position") != std::string::npos);
    REQUIRE(message.find("[6]") != std::string::npos);
    REQUIRE(message.find("ring_root") != std::string::npos);
    REQUIRE(message.find("1601") != std::string::npos);

    auto under = midpoint_raw();
    under[4] = static_cast<int16_t>(kJointSpecs[4].minimum - 1);
    REQUIRE_THROWS_AS(validate_raw_commands(under, "position"), linkerbot::hand::ValidationError);

    auto wrist = midpoint_raw();
    wrist[16] = 1001;
    REQUIRE_THROWS_AS(validate_raw_commands(wrist, "position"), linkerbot::hand::ValidationError);
    wrist[16] = 1000;
    REQUIRE_NOTHROW(validate_raw_commands(wrist, "position"));
}

// 电流限制 60..800、速度 1..250：端点接受，端点外一档拒。
TEST_CASE("comm: validate_u16_range covers current and speed limits", "[comm][l30][joints]") {
    REQUIRE(kCurrentLimitMin == 60);
    REQUIRE(kCurrentLimitMax == 800);
    REQUIRE(kSpeedMin == 1);
    REQUIRE(kSpeedMax == 250);

    REQUIRE_NOTHROW(validate_u16_range(filled_u16(60), kCurrentLimitMin, kCurrentLimitMax, "current_limits"));
    REQUIRE_NOTHROW(validate_u16_range(filled_u16(800), kCurrentLimitMin, kCurrentLimitMax, "current_limits"));
    REQUIRE_THROWS_AS(validate_u16_range(filled_u16(59), kCurrentLimitMin, kCurrentLimitMax, "current_limits"),
                      linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(validate_u16_range(filled_u16(801), kCurrentLimitMin, kCurrentLimitMax, "current_limits"),
                      linkerbot::hand::ValidationError);

    REQUIRE_NOTHROW(validate_u16_range(filled_u16(1), kSpeedMin, kSpeedMax, "speed"));
    REQUIRE_NOTHROW(validate_u16_range(filled_u16(250), kSpeedMin, kSpeedMax, "speed"));
    REQUIRE_THROWS_AS(validate_u16_range(filled_u16(0), kSpeedMin, kSpeedMax, "speed"),
                      linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(validate_u16_range(filled_u16(251), kSpeedMin, kSpeedMax, "speed"),
                      linkerbot::hand::ValidationError);

    auto values = filled_u16(100);
    values[9] = 801;
    const auto message = invalid_argument_message(
        [&] { validate_u16_range(values, kCurrentLimitMin, kCurrentLimitMax, "current_limits"); });
    REQUIRE(message.find("current_limits") != std::string::npos);
    REQUIRE(message.find("[9]") != std::string::npos);
    REQUIRE(message.find("between 60 and 800") != std::string::npos);
    REQUIRE(message.find("801") != std::string::npos);
}

// 电流 6.5 mA/count、速度 0.732 rpm/count，端点换算成工程量。
TEST_CASE("comm: current and speed scale factors convert counts", "[comm][l30][joints]") {
    REQUIRE(kCurrentMilliampsPerCount == Catch::Approx(6.5F));
    REQUIRE(kSpeedRpmPerCount == Catch::Approx(0.732F));

    REQUIRE(static_cast<float>(kCurrentLimitMin) * kCurrentMilliampsPerCount == Catch::Approx(390.0F));
    REQUIRE(static_cast<float>(kCurrentLimitMax) * kCurrentMilliampsPerCount == Catch::Approx(5200.0F));
    REQUIRE(100.0F * kCurrentMilliampsPerCount == Catch::Approx(650.0F));

    REQUIRE(static_cast<float>(kSpeedMin) * kSpeedRpmPerCount == Catch::Approx(0.732F));
    REQUIRE(static_cast<float>(kSpeedMax) * kSpeedRpmPerCount == Catch::Approx(183.0F));
    REQUIRE(50.0F * kSpeedRpmPerCount == Catch::Approx(36.6F));
}
