#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <linkerbot/hand/l20/force_sensor.hpp>

#include "hand/l20/l20_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l20;

namespace {

    constexpr auto kPromptWait = std::chrono::milliseconds{3000};

    std::string finger_label(const std::string &name) {
        if (name == "thumb") {
            return "拇指";
        }
        if (name == "index") {
            return "食指";
        }
        if (name == "middle") {
            return "中指";
        }
        if (name == "ring") {
            return "无名指";
        }
        if (name == "pinky") {
            return "小指";
        }
        return name;
    }

    uint64_t matrix_sum(const ForceSensorData &data) {
        uint64_t sum = 0;
        for (const auto &frame : data.values) {
            for (const uint8_t byte : frame) {
                sum += byte;
            }
        }
        return sum;
    }

    ForceSensorData wait_then_read(SingleForceSensorManager &finger) {
        std::cout << "3 秒后开始读取，请保持当前姿势…\n" << std::flush;
        std::this_thread::sleep_for(kPromptWait);
        return finger.get_blocking(std::chrono::milliseconds{2000});
    }

} // namespace

// 真机点阵力传感器。五指各发 0xB1–0xB5，BYTE1=0xC6 表示 12 行 × 6 列；超时 2s（比关节读更长）。
// 自动用例只检查形状/能读到；按压是否变大要靠下面的交互用例。

// --- 自动：程序自己判矩阵形状 ---

// 一次读齐五指；字段都在就能编译通过（这里没有数值断言）。
TEST_CASE("ForceSensor: get_blocking returns all fingers", "[l20][sensor][force_sensor]") {
    auto &hand = test_util::l20_hand();
    auto data = hand.force_sensor.get_blocking(std::chrono::milliseconds{2000});

    (void)data.thumb;
    (void)data.index;
    (void)data.middle;
    (void)data.ring;
    (void)data.pinky;
}

// 拇指矩阵应是 12 帧 × 每帧 6 字节（协议 12×6 点阵）。
TEST_CASE("ForceSensor: each finger correct shape", "[l20][sensor][force_sensor]") {
    auto &hand = test_util::l20_hand();
    auto data = hand.force_sensor.get_blocking(std::chrono::milliseconds{2000});

    REQUIRE(ForceSensorData::kFrameCount == 12);
    REQUIRE(ForceSensorData::kBytesPerFrame == 6);
    REQUIRE(data.thumb.values.size() == 12);
    REQUIRE(data.thumb.values[0].size() == 6);
}

// get_finger("thumb") 单指阻塞读取，形状同样是 12×6。
TEST_CASE("ForceSensor: single finger blocking", "[l20][sensor][force_sensor]") {
    auto &hand = test_util::l20_hand();
    auto &thumb = hand.force_sensor.get_finger("thumb");
    auto data = thumb.get_blocking(std::chrono::milliseconds{2000});

    REQUIRE(data.values.size() == ForceSensorData::kFrameCount);
    REQUIRE(data.values[0].size() == ForceSensorData::kBytesPerFrame);
}

// 五个名字都能 get_finger，各自读一帧矩阵。
TEST_CASE("ForceSensor: all five fingers accessible", "[l20][sensor][force_sensor]") {
    auto &hand = test_util::l20_hand();
    const std::vector<std::string> fingers = {"thumb", "index", "middle", "ring", "pinky"};
    for (const auto &name : fingers) {
        auto &finger = hand.force_sensor.get_finger(name);
        auto data = finger.get_blocking(std::chrono::milliseconds{2000});
        REQUIRE(data.values.size() == ForceSensorData::kFrameCount);
    }
}

// 读过之后，整机快照里 force_sensor 应有值。
TEST_CASE("ForceSensor: snapshot populated after read", "[l20][sensor][force_sensor]") {
    auto &hand = test_util::l20_hand();
    (void)hand.force_sensor.get_blocking(std::chrono::milliseconds{2000});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.force_sensor.has_value());
}

// 张开后连读两次，五指都是 12×6，且每指第二次 timestamp 更晚。
TEST_CASE("ForceSensor: timestamps advance on successive reads", "[l20][sensor][force_sensor]") {
    auto &hand = test_util::l20_hand();
    test_util::move_and_wait(hand, test_util::open_pose());

    auto data1 = hand.force_sensor.get_blocking(std::chrono::milliseconds{2000});
    REQUIRE(data1.thumb.values.size() == ForceSensorData::kFrameCount);
    REQUIRE(data1.index.values.size() == ForceSensorData::kFrameCount);
    REQUIRE(data1.middle.values.size() == ForceSensorData::kFrameCount);
    REQUIRE(data1.ring.values.size() == ForceSensorData::kFrameCount);
    REQUIRE(data1.pinky.values.size() == ForceSensorData::kFrameCount);

    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    auto data2 = hand.force_sensor.get_blocking(std::chrono::milliseconds{2000});
    REQUIRE(data2.thumb.timestamp > data1.thumb.timestamp);
    REQUIRE(data2.index.timestamp > data1.index.timestamp);
    REQUIRE(data2.middle.timestamp > data1.middle.timestamp);
    REQUIRE(data2.ring.timestamp > data1.ring.timestamp);
    REQUIRE(data2.pinky.timestamp > data1.pinky.timestamp);
}

// --- 交互：动手按传感器后你按 y/n ---

// 先不碰拇指记 baseline，再用力按，看点阵字节和是否变大。
TEST_CASE("ForceSensor: interactive thumb pressure detection", "[l20][sensor][force_sensor][interactive]") {
    auto &hand = test_util::l20_hand();

    test_util::InteractiveSession session("l20_force_sensor_thumb_pressure");

    uint64_t baseline_sum = 0;
    uint64_t pressed_sum = 0;

    session
        .step(
            "请不要触摸拇指传感器",
            [&] {
                auto &thumb = hand.force_sensor.get_finger("thumb");
                auto data = wait_then_read(thumb);
                baseline_sum = matrix_sum(data);
                std::cout << "基线总和: " << baseline_sum << "\n";
            },
            "应记录到未按压时的基线读数")
        .step(
            "请用力按住拇指传感器",
            [&] {
                auto &thumb = hand.force_sensor.get_finger("thumb");
                auto data = wait_then_read(thumb);
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
TEST_CASE("ForceSensor: interactive each finger pressure", "[l20][sensor][force_sensor][interactive]") {
    auto &hand = test_util::l20_hand();

    test_util::InteractiveSession session("l20_force_sensor_each_finger");

    const std::vector<std::string> fingers = {"thumb", "index", "middle", "ring", "pinky"};
    for (const auto &name : fingers) {
        const auto label = finger_label(name);
        session.step(
            "请按住" + label + "传感器",
            [&hand, name, label] {
                auto &finger = hand.force_sensor.get_finger(name);
                auto data = wait_then_read(finger);
                std::cout << label << "传感器总和: " << matrix_sum(data) << "\n";
            },
            label + "传感器应出现压力读数");
    }

    session.run();
    session.save_report();

    REQUIRE(session.failed_steps().empty());
}
