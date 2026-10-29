#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/angle_mapping.hpp>
#include <linkerbot/hand/l20lite/angle_manager.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::hand::AngleMapping;
using linkerbot::hand::default_angle_mapping;
using linkerbot::hand::l20lite::Angle;
using linkerbot::hand::l20lite::AngleManager;

namespace {

    AngleMapping reverse_mapping(std::size_t joint_count) {
        auto mapping = default_angle_mapping(joint_count);
        for (auto &row : mapping) {
            std::reverse(row.begin(), row.end());
        }
        return mapping;
    }

} // namespace

// 百分比映射在拆成 0x01/0x04 两帧之前作用于整条 10 关节向量。

// 原生协议值原样拆成 0x01 前六关节 + 0x04 后四关节。
TEST_CASE("comm: l20lite set_angles sends native values unchanged", "[comm][l20lite][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x01, 6, 0x04, 4, 0));
    CANDispatcher dispatcher(std::move(transport));

    AngleManager manager(l20lite_comm::kLeftId, dispatcher, test_util::unique_mapping_path(), "left", "can0");
    manager.start();
    manager.set_angles({0, 1, 2, 3, 4, 5, 6, 7, 8, 255});

    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent.size() >= 2);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x01, 0, 1, 2, 3, 4, 5});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x04, 6, 7, 8, 255});
    manager.stop();
    dispatcher.stop();
}

// 原生协议值绕过自定义映射。
TEST_CASE("comm: l20lite set_angles bypasses custom mapping", "[comm][l20lite][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x01, 6, 0x04, 4, 0));
    CANDispatcher dispatcher(std::move(transport));

    AngleManager manager(l20lite_comm::kLeftId, dispatcher, test_util::unique_mapping_path(), "left", "can0");
    manager.start();
    manager.set_angle_mapping(reverse_mapping(10));
    manager.set_angles({0, 1, 2, 3, 4, 5, 6, 7, 8, 255});

    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x01, 0, 1, 2, 3, 4, 5});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x04, 6, 7, 8, 255});
    manager.stop();
    dispatcher.stop();
}

// set_angles_percent 先把百分比转 0–255，再经反向映射后拆成两帧。
TEST_CASE("comm: l20lite set_angles_percent converts then maps", "[comm][l20lite][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x01, 6, 0x04, 4, 0));
    CANDispatcher dispatcher(std::move(transport));

    AngleManager manager(l20lite_comm::kLeftId, dispatcher, test_util::unique_mapping_path(), "left", "can0");
    manager.start();
    manager.set_angle_mapping(reverse_mapping(10));
    manager.set_angles_percent(std::array<float, 10>{0, 20, 40, 60, 80, 100, 0, 20, 40, 100});

    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x01, 255, 204, 153, 102, 51, 0});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x04, 255, 204, 153, 0});
    manager.stop();
    dispatcher.stop();
}

// 映射按左右手 + CAN 接口分别持久化，互不覆盖。
TEST_CASE("comm: l20lite angle mapping is persisted by side and interface", "[comm][l20lite][angle]") {
    const auto path = test_util::unique_mapping_path();
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));

    AngleManager left_can0(l20lite_comm::kLeftId, dispatcher, path, "left", "can0");
    left_can0.set_angle_mapping(reverse_mapping(10));

    AngleManager right_can0(0x27, dispatcher, path, "right", "can0");
    AngleManager left_can1(l20lite_comm::kLeftId, dispatcher, path, "left", "can1");

    REQUIRE(left_can0.get_angle_mapping()[0][0] == 255);
    REQUIRE(right_can0.get_angle_mapping()[0][0] == 0);
    REQUIRE(left_can1.get_angle_mapping()[0][0] == 0);
}

// get 连发 0x01、0x04；填充 0x80 读回约 50%。
TEST_CASE("comm: l20lite get_blocking reads 0x01 and 0x04", "[comm][l20lite][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x01, 6, 0x04, 4, 0x80));
    CANDispatcher dispatcher(std::move(transport));

    AngleManager manager(l20lite_comm::kLeftId, dispatcher, test_util::unique_mapping_path(), "left", "can0");
    manager.start();

    const auto data = manager.get_blocking(std::chrono::milliseconds{500});
    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x01});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x04});
    for (auto v : data.angle.to_array()) {
        REQUIRE(v == Catch::Approx(128.0f * 100.0f / 255.0f));
    }

    manager.stop();
    dispatcher.stop();
}
