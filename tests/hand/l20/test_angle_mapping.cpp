#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/angle_mapping.hpp>
#include <linkerbot/hand/l20/l20.hpp>

#include "hand/l20/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::AngleMapping;
using linkerbot::hand::default_angle_mapping;
using linkerbot::hand::l20::L20;
using linkerbot::hand::l20::Side;

namespace {

    AngleMapping reverse_mapping(std::size_t joint_count) {
        auto mapping = default_angle_mapping(joint_count);
        for (auto &row : mapping) {
            std::reverse(row.begin(), row.end());
        }
        return mapping;
    }

} // namespace

// 百分比映射在拆成 0x41–0x45 五帧之前作用于整条 16 关节向量；保留槽保持 0x00。

// 原生协议值按拇指/四指布局拆成五帧，保留位为 0x00。
TEST_CASE("comm: l20 set_angles sends native values unchanged", "[comm][l20][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, 0x41, 0));
    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.angle.set_angles({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});

    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    const auto n = sent.size();
    REQUIRE(sent[n - 5] == std::vector<uint8_t>{0x41, 0, 1, 2, 0x00, 0x00, 3});
    REQUIRE(sent[n - 4] == std::vector<uint8_t>{0x42, 4, 0x00, 5, 0x00, 0x00, 6});
    REQUIRE(sent[n - 3] == std::vector<uint8_t>{0x43, 7, 0x00, 8, 0x00, 0x00, 9});
    REQUIRE(sent[n - 2] == std::vector<uint8_t>{0x44, 10, 0x00, 11, 0x00, 0x00, 12});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x45, 13, 0x00, 14, 0x00, 0x00, 15});
}

// 原生协议值绕过自定义映射，保留槽仍是 0x00。
TEST_CASE("comm: l20 set_angles bypasses custom mapping", "[comm][l20][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, 0x41, 0));
    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.angle.set_angle_mapping(reverse_mapping(16));
    hand.angle.set_angles({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});

    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    const auto n = sent.size();
    REQUIRE(sent[n - 5] == std::vector<uint8_t>{0x41, 0, 1, 2, 0x00, 0x00, 3});
    REQUIRE(sent[n - 4] == std::vector<uint8_t>{0x42, 4, 0x00, 5, 0x00, 0x00, 6});
    REQUIRE(sent[n - 3] == std::vector<uint8_t>{0x43, 7, 0x00, 8, 0x00, 0x00, 9});
    REQUIRE(sent[n - 2] == std::vector<uint8_t>{0x44, 10, 0x00, 11, 0x00, 0x00, 12});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x45, 13, 0x00, 14, 0x00, 0x00, 15});
}

// set_angles_percent 先把百分比转 0–255，再经反向映射后拆成五帧。
TEST_CASE("comm: l20 set_angles_percent converts then maps", "[comm][l20][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, 0x41, 0));
    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.angle.set_angle_mapping(reverse_mapping(16));
    hand.angle.set_angles_percent(
        std::array<float, 16>{0, 20, 40, 60, 80, 100, 0, 20, 40, 60, 80, 100, 0, 20, 40, 100});

    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    const auto n = sent.size();
    REQUIRE(sent[n - 5] == std::vector<uint8_t>{0x41, 255, 204, 153, 0x00, 0x00, 102});
    REQUIRE(sent[n - 4] == std::vector<uint8_t>{0x42, 51, 0x00, 0, 0x00, 0x00, 255});
    REQUIRE(sent[n - 3] == std::vector<uint8_t>{0x43, 204, 0x00, 153, 0x00, 0x00, 102});
    REQUIRE(sent[n - 2] == std::vector<uint8_t>{0x44, 51, 0x00, 0, 0x00, 0x00, 255});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x45, 204, 0x00, 153, 0x00, 0x00, 0});
}

// 映射按左右手 + CAN 接口分别持久化，互不覆盖。
TEST_CASE("comm: l20 angle mapping is persisted by side and interface", "[comm][l20][angle]") {
    const auto path = test_util::unique_mapping_path();

    auto left_transport = std::make_unique<FakeTransport>();
    L20 left_can0(Side::kLeft, std::move(left_transport), "can0", path);
    left_can0.angle.set_angle_mapping(reverse_mapping(16));

    auto right_transport = std::make_unique<FakeTransport>();
    L20 right_can0(Side::kRight, std::move(right_transport), "can0", path);
    auto can1_transport = std::make_unique<FakeTransport>();
    L20 left_can1(Side::kLeft, std::move(can1_transport), "can1", path);

    REQUIRE(left_can0.angle.get_angle_mapping()[0][0] == 255);
    REQUIRE(right_can0.angle.get_angle_mapping()[0][0] == 0);
    REQUIRE(left_can1.angle.get_angle_mapping()[0][0] == 0);
}

// get 连发 0x41–0x45；填充 0x80 读回约 50%。
TEST_CASE("comm: l20 mapping get_blocking reads 0x41 through 0x45", "[comm][l20][angle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, 0x41, 0x80));
    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    const auto n = sent.size();
    REQUIRE(sent[n - 5] == std::vector<uint8_t>{0x41});
    REQUIRE(sent[n - 4] == std::vector<uint8_t>{0x42});
    REQUIRE(sent[n - 3] == std::vector<uint8_t>{0x43});
    REQUIRE(sent[n - 2] == std::vector<uint8_t>{0x44});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x45});
    auto arr = data.angle.to_array();
    REQUIRE(arr.size() == 16);
    for (auto v : arr) {
        REQUIRE(v == Catch::Approx(128.0f * 100.0f / 255.0f));
    }
}
