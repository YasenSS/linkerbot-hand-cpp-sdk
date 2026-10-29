#include <catch2/catch_test_macros.hpp>

#include "hand/l6/l6_fixture.hpp"

using namespace linkerbot::hand::l6;

// 真机版本。0xC0 串码、0xC1 PCB、0xC2 固件、0xC4 结构；get_device_info 一次拉齐，超时 2s。

// 四项都有：串码非空，三个版本 major ≥ 0。
TEST_CASE("Version: get_device_info returns complete", "[l6][lifecycle][version]") {
    auto &hand = test_util::l6_hand();
    auto info = hand.version.get_device_info(std::chrono::milliseconds{2000});

    REQUIRE_FALSE(info.serial_number.empty());
    REQUIRE(info.pcb_version.major >= 0);
    REQUIRE(info.firmware_version.major >= 0);
    REQUIRE(info.mechanical_version.major >= 0);
}

// 手背串码不能是空字符串。
TEST_CASE("Version: serial number non-empty", "[l6][lifecycle][version]") {
    auto &hand = test_util::l6_hand();
    auto info = hand.version.get_device_info(std::chrono::milliseconds{2000});
    REQUIRE_FALSE(info.serial_number.empty());
}

// 固件主版本 > 0，三个版本 to_string 都非空。
TEST_CASE("Version: version fields valid", "[l6][lifecycle][version]") {
    auto &hand = test_util::l6_hand();
    auto info = hand.version.get_device_info(std::chrono::milliseconds{2000});

    REQUIRE(info.firmware_version.major > 0);
    REQUIRE_FALSE(info.pcb_version.to_string().empty());
    REQUIRE_FALSE(info.firmware_version.to_string().empty());
    REQUIRE_FALSE(info.mechanical_version.to_string().empty());
}
