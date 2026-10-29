#include <catch2/catch_test_macros.hpp>

#include <thread>

#include "hand/l20/l20_fixture.hpp"

using namespace linkerbot::hand::l20;

// 真机版本。0xC0 串码（BYTE1 为 frame_id 0..3）、0xC1 PCB、0xC2 固件、0xC4 结构。

// 串码非空，固件 major > 0，三个版本 to_string 都非空。
TEST_CASE("Version: get_device_info returns complete", "[l20][sensor][version]") {
    auto &hand = test_util::l20_hand();
    auto info = hand.version.get_device_info(std::chrono::milliseconds{2000});

    REQUIRE_FALSE(info.serial_number.empty());
    REQUIRE(info.firmware_version.major > 0);
    REQUIRE_FALSE(info.pcb_version.to_string().empty());
    REQUIRE_FALSE(info.firmware_version.to_string().empty());
    REQUIRE_FALSE(info.mechanical_version.to_string().empty());
}

// 手背串码不能是空字符串，长度应合理。
TEST_CASE("Version: serial number non-empty", "[l20][sensor][version]") {
    auto &hand = test_util::l20_hand();
    auto info = hand.version.get_device_info(std::chrono::milliseconds{2000});
    REQUIRE_FALSE(info.serial_number.empty());
    REQUIRE(info.serial_number.size() < 100);
}

// 固件主版本 > 0，三个版本 to_string 都非空。
TEST_CASE("Version: version fields valid", "[l20][sensor][version]") {
    auto &hand = test_util::l20_hand();
    auto info = hand.version.get_device_info(std::chrono::milliseconds{2000});

    REQUIRE(info.firmware_version.major > 0);
    REQUIRE_FALSE(info.pcb_version.to_string().empty());
    REQUIRE_FALSE(info.firmware_version.to_string().empty());
    REQUIRE_FALSE(info.mechanical_version.to_string().empty());
}

// 两次 get_device_info，第二次 timestamp 应不早于第一次。
TEST_CASE("Version: device info has timestamp", "[l20][sensor][version]") {
    auto &hand = test_util::l20_hand();
    auto info1 = hand.version.get_device_info(std::chrono::milliseconds{2000});
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    auto info2 = hand.version.get_device_info(std::chrono::milliseconds{2000});
    REQUIRE(info2.timestamp >= info1.timestamp);
}
