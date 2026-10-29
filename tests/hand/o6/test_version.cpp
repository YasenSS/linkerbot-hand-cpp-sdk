#include <catch2/catch_test_macros.hpp>

#include "hand/o6/o6_fixture.hpp"

using namespace linkerbot::hand::o6;

// 真机版本。0xC0 串码、0xC1 PCB、0xC2 固件、0xC4 结构；get_device_info 一次拉齐，超时 2s。

// get_device_info 一次拉齐：串号非空，三套版本 major 合法。
TEST_CASE("Version: get_device_info returns complete", "[o6][lifecycle][version]") {
    auto &hand = test_util::o6_hand();
    auto info = hand.version.get_device_info(std::chrono::milliseconds{2000});

    REQUIRE_FALSE(info.serial_number.empty());
    REQUIRE(info.pcb_version.major >= 0);
    REQUIRE(info.firmware_version.major >= 0);
    REQUIRE(info.mechanical_version.major >= 0);
}

// 串号不能是空串。
TEST_CASE("Version: serial number non-empty", "[o6][lifecycle][version]") {
    auto &hand = test_util::o6_hand();
    auto info = hand.version.get_device_info(std::chrono::milliseconds{2000});
    REQUIRE_FALSE(info.serial_number.empty());
}

// 固件 major>0，三套版本 to_string 非空。
TEST_CASE("Version: version fields valid", "[o6][lifecycle][version]") {
    auto &hand = test_util::o6_hand();
    auto info = hand.version.get_device_info(std::chrono::milliseconds{2000});

    REQUIRE(info.firmware_version.major > 0);
    REQUIRE_FALSE(info.pcb_version.to_string().empty());
    REQUIRE_FALSE(info.firmware_version.to_string().empty());
    REQUIRE_FALSE(info.mechanical_version.to_string().empty());
}
