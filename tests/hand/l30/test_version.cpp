#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "hand/l30/l30_fixture.hpp"

using namespace linkerbot::hand::l30;

namespace {
    constexpr auto kReadTimeout = std::chrono::milliseconds{2000};
}

// 真机设备身份。config 父命令下的四个只读查询：18 字节 DeviceInfo、变长 ASCII 产品码、
// 节点号、左右手；都不需要先解锁配置。读到的值一并打进报告里方便对照实物铭牌。

// DeviceInfo 十八个字节全解出来：产品号 0x13、串号非零、三个版本 to_string 非空。
TEST_CASE("Version: get_device_info returns complete", "[l30][lifecycle][version]") {
    auto &hand = test_util::l30_hand();
    const auto info = hand.version.get_device_info(kReadTimeout);

    std::cout << "产品号: 0x" << std::hex << static_cast<unsigned>(info.product_id) << std::dec << "\n";
    std::cout << "串号: " << info.serial_number << "\n";
    std::cout << "软件版本: " << info.software_version.to_string() << "\n";
    std::cout << "硬件版本: " << info.hardware_version.to_string() << "\n";
    std::cout << "结构版本: " << info.structure_version.to_string() << "\n";
    std::cout << "节点号: " << static_cast<unsigned>(info.node_id) << "\n";
    std::cout << "左右手: " << hand_side_name(info.hand_side) << "\n";
    std::cout << "传感器型号: " << info.sensor_type << "，产地: " << info.origin << "\n";

    REQUIRE(info.product_id == 0x13);
    REQUIRE(info.serial_number > 0);
    REQUIRE_FALSE(info.software_version.to_string().empty());
    REQUIRE_FALSE(info.hardware_version.to_string().empty());
    REQUIRE_FALSE(info.structure_version.to_string().empty());
}

// 产品码是形如 LHT30-06-001-L-B-3-A 的 ASCII 串，不能是空的。
TEST_CASE("Version: product code is non-empty ASCII", "[l30][lifecycle][version]") {
    auto &hand = test_util::l30_hand();
    const auto code = hand.version.get_product_code(kReadTimeout);
    std::cout << "产品码: " << code << "\n";

    REQUIRE_FALSE(code.empty());
    for (const char letter : code) {
        REQUIRE(static_cast<unsigned char>(letter) <= 0x7FU);
    }
}

// 单独查出来的节点号要和当前用来通信的节点号一致。
TEST_CASE("Version: node id matches the addressed node", "[l30][lifecycle][version]") {
    auto &hand = test_util::l30_hand();
    const auto node_id = hand.version.get_node_id(kReadTimeout);
    std::cout << "节点号: " << static_cast<unsigned>(node_id) << "\n";

    REQUIRE(node_id >= protocol::kNodeIdMin);
    REQUIRE(node_id <= protocol::kNodeIdMax);
    REQUIRE(node_id == hand.node_id());
}

// 单独的左右手查询要和 DeviceInfo 里的 hand_side 对得上。
TEST_CASE("Version: hand side agrees with device info", "[l30][lifecycle][version]") {
    auto &hand = test_util::l30_hand();
    const auto side = hand.version.get_hand_side(kReadTimeout);
    const auto info = hand.version.get_device_info(kReadTimeout);
    std::cout << "左右手: " << hand_side_name(side) << "\n";

    REQUIRE(side == info.hand_side);
}

// 两次 get_device_info，第二次 timestamp 应不早于第一次。
TEST_CASE("Version: device info has timestamp", "[l30][lifecycle][version]") {
    auto &hand = test_util::l30_hand();
    const auto first = hand.version.get_device_info(kReadTimeout);
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    const auto second = hand.version.get_device_info(kReadTimeout);

    REQUIRE(second.timestamp >= first.timestamp);
}
