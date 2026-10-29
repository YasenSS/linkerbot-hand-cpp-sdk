#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include "hand/l30/l30_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l30;

namespace {

    constexpr auto kReadTimeout = std::chrono::milliseconds{2000};
    constexpr auto kPromptWait = std::chrono::milliseconds{3000};

    uint64_t matrix_sum(const FingerForceData &data) {
        uint64_t sum = 0;
        for (const auto &row : data.values) {
            for (const uint8_t byte : row) {
                sum += byte;
            }
        }
        return sum;
    }

    std::string finger_label(Finger finger) {
        switch (finger) {
        case Finger::kThumb:
            return "拇指";
        case Finger::kIndex:
            return "食指";
        case Finger::kMiddle:
            return "中指";
        case Finger::kRing:
            return "无名指";
        case Finger::kPinky:
            return "小指";
        }
        return std::string(finger_name(finger));
    }

    FingerForceData wait_then_read(L30 &hand, Finger finger) {
        std::cout << "3 秒后开始读取，请保持当前姿势…\n" << std::flush;
        std::this_thread::sleep_for(kPromptWait);
        return hand.force_sensor.get_finger(finger, kReadTimeout);
    }

} // namespace

// 真机点阵力传感器。一次读发一帧空载荷请求，设备回 0x10/0x11 两帧共 72 字节，
// 拼成 12 行 × 6 列。自动用例只看形状与数值是否落在 0–255 的合理范围，
// 按压是否变大靠交互用例：先做拇指基线对比，再拇→食→中→无名→小逐指确认。

// --- 自动：程序自己判矩阵形状 ---

// 一次读齐五指，每个手指都带回自己的 Finger 标记与 12×6 矩阵。
TEST_CASE("ForceSensor: get_blocking returns all five fingers", "[l30][sensor][force]") {
    auto &hand = test_util::l30_hand();
    hand.control.enable(std::chrono::milliseconds{1000});
    const auto data = hand.force_sensor.get_blocking(kReadTimeout);

    REQUIRE(data.thumb.finger == Finger::kThumb);
    REQUIRE(data.index.finger == Finger::kIndex);
    REQUIRE(data.middle.finger == Finger::kMiddle);
    REQUIRE(data.ring.finger == Finger::kRing);
    REQUIRE(data.pinky.finger == Finger::kPinky);
    REQUIRE(data.thumb.values.size() == FingerForceData::kRows);
    REQUIRE(data.thumb.values[0].size() == FingerForceData::kColumns);
}

// 逐指单读，形状固定是 12 行 × 6 列。
TEST_CASE("ForceSensor: each finger reads a 12x6 matrix", "[l30][sensor][force]") {
    auto &hand = test_util::l30_hand();
    for (const auto finger : kAllFingers) {
        const auto data = hand.force_sensor.get_finger(finger, kReadTimeout);
        REQUIRE(data.finger == finger);
        REQUIRE(data.values.size() == 12);
        REQUIRE(data.values[0].size() == 6);
    }
}

// 未受力时五指矩阵总和都应远小于满量程（72 × 255），说明读到的是真实基线而不是全 0xFF。
TEST_CASE("ForceSensor: idle readings are plausible", "[l30][sensor][force]") {
    auto &hand = test_util::l30_hand();
    for (const auto finger : kAllFingers) {
        const auto data = hand.force_sensor.get_finger(finger, kReadTimeout);
        const auto sum = matrix_sum(data);
        std::cout << finger_label(finger) << "基线总和: " << sum << "\n";
        INFO(finger_label(finger));
        REQUIRE(sum < 72U * 255U);
    }
}

// 读过之后，整机快照里 force_sensor 应有值。
TEST_CASE("ForceSensor: snapshot populated after get_blocking", "[l30][sensor][force]") {
    auto &hand = test_util::l30_hand();
    (void)hand.force_sensor.get_blocking(kReadTimeout);
    REQUIRE(hand.get_snapshot().force_sensor.has_value());
}

// --- 交互：动手按传感器后你按 y/n ---

// 先不碰拇指记 baseline，再用力按，看点阵字节和是否变大。
TEST_CASE("ForceSensor: interactive thumb pressure detection", "[l30][sensor][force][interactive]") {
    auto &hand = test_util::l30_hand();
    test_util::InteractiveSession session("l30_force_sensor_thumb_pressure");

    uint64_t baseline_sum = 0;
    uint64_t pressed_sum = 0;

    session
        .step(
            "请不要触摸拇指传感器",
            [&] {
                const auto data = wait_then_read(hand, Finger::kThumb);
                baseline_sum = matrix_sum(data);
                std::cout << "基线总和: " << baseline_sum << "\n";
            },
            "应记录到未按压时的基线读数")
        .step(
            "请用力按住拇指传感器",
            [&] {
                const auto data = wait_then_read(hand, Finger::kThumb);
                pressed_sum = matrix_sum(data);
                std::cout << "按压总和: " << pressed_sum << "\n";
                std::cout << "差值: " << static_cast<int64_t>(pressed_sum) - static_cast<int64_t>(baseline_sum) << "\n";
            },
            "按压后的总和应明显高于基线");

    session.run();
    session.save_report();

    REQUIRE(session.failed_steps().empty());
}

// 拇→食→中→无名→小依次按，每指读一次并打印点阵字节和。
TEST_CASE("ForceSensor: interactive each finger pressure", "[l30][sensor][force][interactive]") {
    auto &hand = test_util::l30_hand();
    test_util::InteractiveSession session("l30_force_sensor_each_finger");

    for (const auto finger : kAllFingers) {
        const auto label = finger_label(finger);
        session.step(
            "请按住" + label + "传感器",
            [&hand, finger, label] {
                const auto data = wait_then_read(hand, finger);
                std::cout << label << "传感器总和: " << matrix_sum(data) << "\n";
            },
            label + "传感器应出现压力读数");
    }

    session.run();
    session.save_report();

    REQUIRE(session.failed_steps().empty());
}
