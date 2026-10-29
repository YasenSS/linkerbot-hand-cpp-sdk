#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/angle_mapping.hpp>
#include <linkerbot/hand/l6/angle_manager.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::hand::AngleMapping;
using linkerbot::hand::default_angle_mapping;
using linkerbot::hand::l6::Angle;
using linkerbot::hand::l6::AngleManager;

namespace {

    constexpr uint32_t kLeftId = 0x28;

    AngleMapping reverse_mapping(std::size_t joint_count) {
        auto mapping = default_angle_mapping(joint_count);
        for (auto &row : mapping) {
            std::reverse(row.begin(), row.end());
        }
        return mapping;
    }

    std::vector<uint8_t> last_payload(FakeTransport *transport) {
        const auto sent = transport->sent();
        REQUIRE_FALSE(sent.empty());
        const auto &msg = sent.back();
        return {msg.data.begin(), msg.data.begin() + msg.dlc};
    }

} // namespace

TEST_CASE("comm: set_angles sends native values unchanged", "[comm][l6][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->echo_cmd(kLeftId, 0x01, Angle::kJointCount);
    CANDispatcher dispatcher(std::move(transport));

    AngleManager manager(kLeftId, dispatcher, test_util::unique_mapping_path(), "left", "can0");
    manager.start();
    manager.set_angles({0, 1, 2, 3, 4, 255});

    REQUIRE(last_payload(raw) == std::vector<uint8_t>{0x01, 0, 1, 2, 3, 4, 255});
    manager.stop();
    dispatcher.stop();
}

// 原生协议值不经过逻辑映射，避免保持姿态时二次映射。
TEST_CASE("comm: set_angles bypasses custom mapping", "[comm][l6][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->echo_cmd(kLeftId, 0x01, Angle::kJointCount);
    CANDispatcher dispatcher(std::move(transport));

    AngleManager manager(kLeftId, dispatcher, test_util::unique_mapping_path(), "left", "can0");
    manager.start();
    manager.set_angle_mapping(reverse_mapping(6));
    manager.set_angles({0, 1, 2, 3, 4, 255});

    REQUIRE(last_payload(raw) == std::vector<uint8_t>{0x01, 0, 1, 2, 3, 4, 255});
    manager.stop();
    dispatcher.stop();
}

TEST_CASE("comm: set_angles_percent converts then maps", "[comm][l6][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->echo_cmd(kLeftId, 0x01, Angle::kJointCount);
    CANDispatcher dispatcher(std::move(transport));

    AngleManager manager(kLeftId, dispatcher, test_util::unique_mapping_path(), "left", "can0");
    manager.start();
    manager.set_angle_mapping(reverse_mapping(6));
    manager.set_angles_percent(std::array<float, 6>{0, 20, 40, 60, 80, 100});

    REQUIRE(last_payload(raw) == std::vector<uint8_t>{0x01, 255, 204, 153, 102, 51, 0});
    manager.stop();
    dispatcher.stop();
}

TEST_CASE("comm: angle mapping is persisted by side and interface", "[comm][l6][angle]") {
    const auto path = test_util::unique_mapping_path();
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));

    AngleManager left_can0(kLeftId, dispatcher, path, "left", "can0");
    left_can0.set_angle_mapping(reverse_mapping(6));

    AngleManager right_can0(0x27, dispatcher, path, "right", "can0");
    AngleManager left_can1(kLeftId, dispatcher, path, "left", "can1");

    REQUIRE(left_can0.get_angle_mapping()[0][0] == 255);
    REQUIRE(right_can0.get_angle_mapping()[0][0] == 0);
    REQUIRE(left_can1.get_angle_mapping()[0][0] == 0);
}
