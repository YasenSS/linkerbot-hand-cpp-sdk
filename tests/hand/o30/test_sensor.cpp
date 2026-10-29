#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <linkerbot/hand/o30/protocol.hpp>

#include "hand/o30/o30_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::o30;
using linkerbot::hand::o30::protocol::ProtocolError;

namespace {
    constexpr auto kMetaTimeout = std::chrono::milliseconds{500};
    constexpr auto kDataTimeout = std::chrono::milliseconds{2000};
    constexpr auto kPromptWait = std::chrono::milliseconds{3000};

    uint64_t normal_force_sum(const SensorData &data) {
        if (data.info.descriptor != "FnS_U16_1;Fn_U8_70" || data.payload.size() != 72) {
            throw ProtocolError("pressure test requires the verified FnS_U16_1;Fn_U8_70 layout");
        }
        // FnS is one little-endian uint16, not the sum of its two bytes.
        return protocol::read_u16_le(data.payload.data());
    }

    constexpr std::array<uint8_t, 5> kFingerSensors{1, 2, 3, 4, 5};

    bool is_v004(O30 &hand) { return hand.version.get_protocol_version(kMetaTimeout) == "0.0.4"; }

    std::string finger_label(uint8_t sensor) {
        switch (sensor) {
        case 1:
            return "拇指";
        case 2:
            return "食指";
        case 3:
            return "中指";
        case 4:
            return "无名指";
        case 5:
            return "小指";
        default:
            return "传感器" + std::to_string(sensor);
        }
    }

    SensorData wait_then_read(O30 &hand) {
        std::cout << "3 秒后开始读取，请保持当前姿势…\n" << std::flush;
        std::this_thread::sleep_for(kPromptWait);
        return hand.sensor.read_data(kDataTimeout);
    }

    struct RestoreSelectedSensor {
        O30 &hand;
        uint8_t original;

        explicit RestoreSelectedSensor(O30 &h) : hand(h), original(h.sensor.get_info(kMetaTimeout).selected) {}

        ~RestoreSelectedSensor() {
            if (original < 1 || original > 5) {
                return;
            }
            try {
                hand.sensor.select(original, kMetaTimeout);
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
        }

        RestoreSelectedSensor(const RestoreSelectedSensor &) = delete;
        RestoreSelectedSensor &operator=(const RestoreSelectedSensor &) = delete;
        RestoreSelectedSensor(RestoreSelectedSensor &&) = delete;
        RestoreSelectedSensor &operator=(RestoreSelectedSensor &&) = delete;
    };
} // namespace

// 真机触觉。MI 0x31 元数据，0x32–0x34 数据通道。
// 旧固件可能是 NO_SENSOR / total_length=0；0.0.4 用 select(1–5) 切拇→小，
// 一次只读当前选中指。自动用例查形状、选路与五指读样；按压靠交互。

// --- 自动：程序自己判元数据与通道 ---

// 0x31 类型字符串非空；0.0.4 描述符布局时校验选路与矩阵上界。
TEST_CASE("Sensor: info is readable", "[o30][sensor]") {
    auto &hand = test_util::o30_hand();
    const auto info = hand.sensor.get_info(kMetaTimeout);
    REQUIRE_FALSE(info.type.empty());
    if (!info.descriptor.empty()) {
        REQUIRE(info.selected >= 1);
        REQUIRE(info.selected <= 5);
        REQUIRE(info.rows <= info.maximum_rows);
        REQUIRE(info.columns <= info.maximum_columns);
        REQUIRE(info.total_length <= 767);
    }
}

// 通道号只能是 1/2/3，0 和 4 应在发帧前被拒。
TEST_CASE("Sensor: read_channel rejects invalid channel", "[o30][sensor]") {
    auto &hand = test_util::o30_hand();
    REQUIRE_THROWS_AS(hand.sensor.read_channel(0, kMetaTimeout), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.sensor.read_channel(4, kMetaTimeout), linkerbot::hand::ValidationError);
}

// 总长为 0 时读通道 / 整包都应抛 ProtocolError，不要去碰 0x32–0x34。
TEST_CASE("Sensor: zero total length skips data channels", "[o30][sensor]") {
    auto &hand = test_util::o30_hand();
    const auto info = hand.sensor.get_info(kMetaTimeout);
    if (info.total_length == 0) {
        REQUIRE_THROWS_AS(hand.sensor.read_channel(1, kMetaTimeout), ProtocolError);
        REQUIRE_THROWS_AS(hand.sensor.read_data(kMetaTimeout), ProtocolError);
    }
}

// 有载荷时 read_data 字节数应等于 total_length，通道 1 非空。
TEST_CASE("Sensor: read_data matches total_length", "[o30][sensor]") {
    auto &hand = test_util::o30_hand();
    const auto info = hand.sensor.get_info(kMetaTimeout);
    if (info.total_length == 0) {
        SKIP("device reports no tactile payload");
    }

    const auto data = hand.sensor.read_data(kDataTimeout);
    REQUIRE(data.payload.size() == info.total_length);
    REQUIRE(data.info.selected == info.selected);
    REQUIRE(data.info.total_length == info.total_length);

    const auto channel = hand.sensor.read_channel(1, kDataTimeout);
    REQUIRE(channel.channel == 1);
    REQUIRE_FALSE(channel.payload.empty());
    REQUIRE(channel.payload.size() <= (is_v004(hand) ? 255 : 256));
}

// 0.0.4 按 255/511 切银行：超出总长的通道应抛，未超出的应读到数据。
TEST_CASE("Sensor: extra banks follow total_length", "[o30][sensor]") {
    auto &hand = test_util::o30_hand();
    const auto info = hand.sensor.get_info(kMetaTimeout);
    if (!is_v004(hand) || info.total_length == 0) {
        SKIP("legacy firmware or no tactile payload");
    }

    if (info.total_length > 255) {
        REQUIRE_FALSE(hand.sensor.read_channel(2, kDataTimeout).payload.empty());
    } else {
        REQUIRE_THROWS_AS(hand.sensor.read_channel(2, kDataTimeout), ProtocolError);
    }

    if (info.total_length > 511) {
        REQUIRE_FALSE(hand.sensor.read_channel(3, kDataTimeout).payload.empty());
    } else {
        REQUIRE_THROWS_AS(hand.sensor.read_channel(3, kDataTimeout), ProtocolError);
    }
}

// 0.0.4 选路必须是 1–5；回写当前选中路应保持不变。
TEST_CASE("Sensor: select round-trips on 0.0.4", "[o30][sensor][configuration]") {
    auto &hand = test_util::o30_hand();
    if (!is_v004(hand)) {
        SKIP("legacy firmware has no 1..5 selection check");
    }

    REQUIRE_THROWS_AS(hand.sensor.select(0, kMetaTimeout), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.sensor.select(6, kMetaTimeout), linkerbot::hand::ValidationError);

    const auto selected = hand.sensor.get_info(kMetaTimeout).selected;
    REQUIRE(selected >= 1);
    REQUIRE(selected <= 5);
    REQUIRE_NOTHROW(hand.sensor.select(selected, kMetaTimeout));
    REQUIRE(hand.sensor.get_info(kMetaTimeout).selected == selected);
}

// 0.0.4 选 1–5 号（拇→小），每指都应读满当前描述符长度；结束恢复原选路。
TEST_CASE("Sensor: each finger reads a complete sample", "[o30][sensor]") {
    auto &hand = test_util::o30_hand();
    if (!is_v004(hand)) {
        SKIP("legacy firmware has no 1..5 finger selection");
    }
    const auto info = hand.sensor.get_info(kMetaTimeout);
    if (info.total_length == 0) {
        SKIP("device reports no tactile payload");
    }

    const RestoreSelectedSensor restore(hand);
    for (const auto sensor : kFingerSensors) {
        INFO("sensor=" << static_cast<unsigned>(sensor) << " " << finger_label(sensor));
        hand.sensor.select(sensor, kMetaTimeout);
        const auto data = hand.sensor.read_data(kDataTimeout);
        REQUIRE(data.info.selected == sensor);
        REQUIRE(data.payload.size() == data.info.total_length);
        if (data.info.descriptor == "FnS_U16_1;Fn_U8_70") {
            REQUIRE(data.payload.size() == 72);
            REQUIRE(data.info.rows == 10);
            REQUIRE(data.info.columns == 7);
            const auto fns = protocol::read_u16_le(data.payload.data());
            std::cout << finger_label(sensor) << "基线 FnS: " << fns << "\n";
        }
    }
}

// --- 交互：动手按传感器后你按 y/n ---

// 先不碰拇指记 baseline，再按压，检查 FnS 的 uint16 小端力值是否变大。
TEST_CASE("Sensor: interactive thumb pressure detection", "[o30][sensor][interactive]") {
    auto &hand = test_util::o30_hand();
    if (!is_v004(hand)) {
        SKIP("legacy firmware has no 1..5 finger selection");
    }
    const auto info = hand.sensor.get_info(kMetaTimeout);
    if (info.total_length == 0) {
        SKIP("device reports no tactile payload");
    }

    const RestoreSelectedSensor restore(hand);
    hand.sensor.select(1, kMetaTimeout);
    const auto thumb = hand.sensor.get_info(kMetaTimeout);
    if (thumb.descriptor != "FnS_U16_1;Fn_U8_70") {
        SKIP("No verified force decoder for this tactile descriptor");
    }

    test_util::InteractiveSession session("o30_sensor_thumb_pressure");
    uint64_t baseline_sum = 0;
    uint64_t pressed_sum = 0;

    session
        .step(
            "请不要触摸拇指传感器",
            [&] {
                const auto data = wait_then_read(hand);
                baseline_sum = normal_force_sum(data);
                std::cout << "基线 FnS: " << baseline_sum << "\n";
            },
            "应记录到未按压时的基线读数")
        .step(
            "请用力按住拇指传感器",
            [&] {
                const auto data = wait_then_read(hand);
                pressed_sum = normal_force_sum(data);
                std::cout << "按压 FnS: " << pressed_sum << "\n";
                std::cout << "差值: " << static_cast<int64_t>(pressed_sum) - static_cast<int64_t>(baseline_sum) << "\n";
            },
            "按压后的 FnS 应明显高于基线");

    session.run();
    session.save_report();
    REQUIRE_FALSE(session.quit_early());
    REQUIRE(session.failed_steps().empty());
    REQUIRE(pressed_sum > baseline_sum);
}

// 拇→食→中→无名→小依次按，每指先 select 再读 FnS。
TEST_CASE("Sensor: interactive each finger pressure", "[o30][sensor][interactive]") {
    auto &hand = test_util::o30_hand();
    if (!is_v004(hand)) {
        SKIP("legacy firmware has no 1..5 finger selection");
    }
    const auto info = hand.sensor.get_info(kMetaTimeout);
    if (info.total_length == 0) {
        SKIP("device reports no tactile payload");
    }

    const RestoreSelectedSensor restore(hand);
    test_util::InteractiveSession session("o30_sensor_each_finger");
    for (const auto sensor : kFingerSensors) {
        const auto label = finger_label(sensor);
        session.step(
            "请按住" + label + "传感器",
            [&hand, sensor, label] {
                hand.sensor.select(sensor, kMetaTimeout);
                const auto data = wait_then_read(hand);
                REQUIRE(data.info.selected == sensor);
                if (data.info.descriptor == "FnS_U16_1;Fn_U8_70") {
                    std::cout << label << "传感器 FnS: " << normal_force_sum(data) << "\n";
                } else {
                    std::cout << label << "传感器 payload=" << data.payload.size()
                              << " descriptor=" << data.info.descriptor << "\n";
                }
            },
            label + "传感器应出现压力读数");
    }

    session.run();
    session.save_report();
    REQUIRE_FALSE(session.quit_early());
    REQUIRE(session.failed_steps().empty());
}
