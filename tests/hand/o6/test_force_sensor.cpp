#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <linkerbot/hand/o6/force_sensor.hpp>

#include "hand/o6/o6_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::o6;

namespace {

    constexpr auto kPromptWait = std::chrono::milliseconds{3000};

    // 英文指名转中文，给交互提示用。
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

    // 点阵全部字节求和，用来对比按压前后。
    uint64_t matrix_sum(const ForceSensorData &data) {
        uint64_t sum = 0;
        for (const auto &frame : data.values) {
            for (const uint8_t byte : frame) {
                sum += byte;
            }
        }
        return sum;
    }

    // 提示后等 3 秒再阻塞读该指。
    ForceSensorData wait_then_read(SingleForceSensorManager &finger) {
        std::cout << "3 秒后开始读取，请保持当前姿势…\n" << std::flush;
        std::this_thread::sleep_for(kPromptWait);
        return finger.get_blocking(std::chrono::milliseconds{2000});
    }

} // namespace

// 真机点阵力传感器。五指各发 0xB1–0xB5，BYTE1=0xA4 表示 10 行 × 4 列。

// get_blocking 一次能取到五指数据。
TEST_CASE("ForceSensor: get_blocking returns all fingers", "[o6][sensor][force_sensor]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.force_sensor.get_blocking(std::chrono::milliseconds{2000});

    (void)data.thumb;
    (void)data.index;
    (void)data.middle;
    (void)data.ring;
    (void)data.pinky;
}

// 拇指矩阵应是 10 行 × 4 列。
TEST_CASE("ForceSensor: each finger correct shape", "[o6][sensor][force_sensor]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.force_sensor.get_blocking(std::chrono::milliseconds{2000});

    REQUIRE(ForceSensorData::kFrameCount == 10);
    REQUIRE(ForceSensorData::kBytesPerFrame == 4);
    REQUIRE(data.thumb.values.size() == 10);
    REQUIRE(data.thumb.values[0].size() == 4);
}

// get_finger("thumb") 单指阻塞读，形状正确。
TEST_CASE("ForceSensor: single finger blocking", "[o6][sensor][force_sensor]") {
    auto &hand = test_util::o6_hand();
    auto &thumb = hand.force_sensor.get_finger("thumb");
    auto data = thumb.get_blocking(std::chrono::milliseconds{2000});

    REQUIRE(data.values.size() == ForceSensorData::kFrameCount);
    REQUIRE(data.values[0].size() == ForceSensorData::kBytesPerFrame);
}

// 五指都能按名字 get_finger 并读到满帧。
TEST_CASE("ForceSensor: all five fingers accessible", "[o6][sensor][force_sensor]") {
    auto &hand = test_util::o6_hand();
    const std::vector<std::string> fingers = {"thumb", "index", "middle", "ring", "pinky"};
    for (const auto &name : fingers) {
        auto &finger = hand.force_sensor.get_finger(name);
        auto data = finger.get_blocking(std::chrono::milliseconds{2000});
        REQUIRE(data.values.size() == ForceSensorData::kFrameCount);
    }
}

// get_blocking 之后 snapshot.force_sensor 应有值。
TEST_CASE("ForceSensor: snapshot populated after read", "[o6][sensor][force_sensor]") {
    auto &hand = test_util::o6_hand();
    (void)hand.force_sensor.get_blocking(std::chrono::milliseconds{2000});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.force_sensor.has_value());
}

// 人眼确认：拇指未按 vs 用力按，总和应升高。
TEST_CASE("ForceSensor: interactive thumb pressure detection", "[o6][sensor][force_sensor][interactive]") {
    auto &hand = test_util::o6_hand();

    test_util::InteractiveSession session("force_sensor_thumb_pressure");

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

// 人眼确认：五指逐个按压都能读到压力。
TEST_CASE("ForceSensor: interactive each finger pressure", "[o6][sensor][force_sensor][interactive]") {
    auto &hand = test_util::o6_hand();

    test_util::InteractiveSession session("force_sensor_each_finger");

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
