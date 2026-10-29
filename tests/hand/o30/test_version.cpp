#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>
#include <string>

#include "hand/o30/o30_fixture.hpp"

using namespace linkerbot::hand::o30;

// 真机产品信息。MI 0x41 为旧 225 字节或 0.0.4 的 249 字节布局。
// 读到的值一并打到终端，方便对照实物铭牌。

namespace {
    void print_product_info(const ProductInfo &info) {
        std::cout << "型号: " << info.model << "\n";
        std::cout << "左右手: " << info.hand_side << "\n";
        std::cout << "协议: " << info.protocol_name << " " << info.protocol_version << "\n";
        std::cout << "应用版本: " << info.app_version << "\n";
        std::cout << "Bootloader: " << info.bootloader_version << "\n";
        std::cout << "结构版本: " << info.mechanical_version << "\n";
        std::cout << "接口硬件: " << info.interface_hw_version << "\n";
        std::cout << "适配器硬件: " << info.adapter_hw_version << "\n";
        std::cout << "控制硬件: " << info.control_hw_version << "\n";
        std::cout << "电压范围: " << info.voltage_range << "\n";
        std::cout << "支持接口: " << info.supported_interfaces << "\n";
        std::cout << "构建时间: " << info.build_time << "\n";
        std::cout << "设备 UID: " << info.device_uid << "\n";
        std::cout << "MCU UID: " << info.mcu_uid << "\n";
    }
} // namespace

// 型号非空，协议名含 HOP，版本字段非空。
TEST_CASE("Version: get_product_info returns complete", "[o30][lifecycle][version]") {
    auto &hand = test_util::o30_hand();
    const auto info = hand.version.get_product_info(std::chrono::milliseconds{2000});
    print_product_info(info);

    REQUIRE_FALSE(info.model.empty());
    REQUIRE(info.protocol_name.find("HOP") != std::string::npos);
    REQUIRE_FALSE(info.protocol_version.empty());
    REQUIRE_FALSE(info.app_version.empty());
    REQUIRE(info.build_time.find('\0') == std::string::npos);
    if (info.protocol_version == "0.0.4") {
        REQUIRE_FALSE(info.build_time.empty());
        REQUIRE(info.build_time.find('-') != std::string::npos);
    }
}

// get_model 应与完整产品信息里的 model 一致。
TEST_CASE("Version: model helper matches product info", "[o30][lifecycle][version]") {
    auto &hand = test_util::o30_hand();
    REQUIRE(hand.version.get_model(std::chrono::milliseconds{500}) ==
            hand.version.get_product_info(std::chrono::milliseconds{2000}).model);
}

// 两次读取，第二次 timestamp 应不早于第一次。
TEST_CASE("Version: device info has timestamp", "[o30][lifecycle][version]") {
    auto &hand = test_util::o30_hand();
    const auto a = hand.version.get_product_info(std::chrono::milliseconds{2000});
    const auto b = hand.version.get_product_info(std::chrono::milliseconds{2000});
    REQUIRE(b.timestamp >= a.timestamp);
}
