#include "hardware/hand_device_connection.hpp"
#include <catch2/catch_test_macros.hpp>
#include <map>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;

namespace {
    using Variables = std::map<std::string, std::string>;
    Environment environment(const Variables &values) {
        return [&values](std::string_view key) {
            const auto entry = values.find(std::string{key});
            if (entry == values.end()) {
                return std::optional<std::string>(std::nullopt);
            }
            return std::optional<std::string>(entry->second);
        };
    }
    Variables pcan_variables() {
        return {{"HAND_DEVICE_BACKEND", "pcan"},
                {"HAND_DEVICE_CHANNEL", "PCAN_USBBUS1"},
                {"HAND_DEVICE_LIBRARY", "missing-acceptance-driver.dll"},
                {"HAND_DEVICE_BITRATE", "1000000"},
                {"HAND_DEVICE_DATA_BITRATE", "5000000"},
                {"HAND_DEVICE_FD_CLOCK", "80000000"},
                {"HAND_DEVICE_NOM_BRP", "1"},
                {"HAND_DEVICE_NOM_TSEG1", "63"},
                {"HAND_DEVICE_NOM_TSEG2", "16"},
                {"HAND_DEVICE_NOM_SJW", "16"},
                {"HAND_DEVICE_DATA_BRP", "1"},
                {"HAND_DEVICE_DATA_TSEG1", "11"},
                {"HAND_DEVICE_DATA_TSEG2", "4"},
                {"HAND_DEVICE_DATA_SJW", "4"}};
    }
}

TEST_CASE("Hardware acceptance numeric inputs are strict", "[comm][hardware-config]") {
    REQUIRE_FALSE(parse_u32(std::nullopt));
    REQUIRE(parse_u32("0") == 0);
    REQUIRE(parse_u32("0x28") == 40);
    REQUIRE(parse_u32("4294967295") == UINT32_MAX);
    for (const auto *value : {"", "-1", "+1", " 1", "1x", "4294967296", "999999999999999999999"}) {
        CAPTURE(value);
        REQUIRE_THROWS(parse_u32(value));
    }
}

#if LINKERBOT_HAND_HAS_SLCAN
TEST_CASE("Hardware SLCAN requires explicit Classic serial configuration without I/O", "[comm][hardware-config]") {
    const Variables complete{{"HAND_DEVICE_BACKEND", "slcan"},
                             {"HAND_DEVICE_CHANNEL", "COM10"},
                             {"HAND_DEVICE_BITRATE", "1000000"},
                             {"HAND_DEVICE_SERIAL_BAUD", "115200"},
                             {"HAND_DEVICE_SERIAL_TIMEOUT_MS", "500"}};
    HandDeviceConfig config;
    config.side = HandSide::kLeft;
    for (const auto model : {Model::kL6, Model::kO6, Model::kL20Lite, Model::kL20}) {
        config.model = model;
        REQUIRE_NOTHROW(hardware_factory(config, environment(complete)));
    }
    for (const auto model : {Model::kL30, Model::kO30}) {
        config.model = model;
        REQUIRE_THROWS(hardware_factory(config, environment(complete)));
    }
    config.model = Model::kL6;
    for (const auto &[key, value] : complete) {
        if (key == "HAND_DEVICE_BACKEND") {
            continue;
        }
        auto values = complete;
        values.erase(key);
        REQUIRE_THROWS(hardware_factory(config, environment(values)));
    }
    for (const auto &[key, value] : Variables{{"HAND_DEVICE_CHANNEL", "COM01"},
                                              {"HAND_DEVICE_SERIAL_BAUD", "0"},
                                              {"HAND_DEVICE_SERIAL_TIMEOUT_MS", "5001"},
                                              {"HAND_DEVICE_BITRATE", "123456"},
                                              {"HAND_DEVICE_LIBRARY", "unused.dll"},
                                              {"HAND_DEVICE_DATA_BITRATE", "5000000"}}) {
        auto values = complete;
        values[key] = value;
        REQUIRE_THROWS(hardware_factory(config, environment(values)));
    }
}
#endif

#ifdef _WIN32
TEST_CASE("Hardware Windows environment paths retain Unicode", "[comm][hardware-config]") {
    const auto *key = L"LINKERBOT_TEST_HARDWARE_UNICODE_PATH";
    REQUIRE(SetEnvironmentVariableW(key, L"C:\\新目录\\PCAN Basic\\PCANBasic.dll"));
    const auto actual = env("LINKERBOT_TEST_HARDWARE_UNICODE_PATH");
    REQUIRE(SetEnvironmentVariableW(key, nullptr));
    REQUIRE(actual == "C:\\新目录\\PCAN Basic\\PCANBasic.dll");
}
#endif

TEST_CASE("Hardware PCAN factories validate all models without loading a driver", "[comm][hardware-config]") {
    const auto values = pcan_variables();
    for (const auto model : {Model::kL6, Model::kO6, Model::kL20Lite, Model::kL20, Model::kL30, Model::kO30}) {
        CAPTURE(model);
        HandDeviceConfig config;
        config.model = model;
        config.side = HandSide::kLeft;
        REQUIRE_NOTHROW(hardware_factory(config, environment(values)));
    }
}

TEST_CASE("Hardware FD acceptance requires explicit rates and timing", "[comm][hardware-config]") {
    HandDeviceConfig config;
    config.model = Model::kL30;
    const auto complete = pcan_variables();
    for (const auto &[key, value] : complete) {
        if (key == "HAND_DEVICE_BACKEND") {
            continue;
        }
        DYNAMIC_SECTION("missing " << key) {
            auto values = complete;
            values.erase(key);
            REQUIRE_THROWS(hardware_factory(config, environment(values)));
        }
        DYNAMIC_SECTION("empty " << key) {
            auto values = complete;
            values[key] = "";
            REQUIRE_THROWS(hardware_factory(config, environment(values)));
        }
    }
    auto values = complete;
    values["HAND_DEVICE_BACKEND"] = "unknown";
    REQUIRE_THROWS(hardware_factory(config, environment(values)));
    values = complete;
    values["HAND_DEVICE_BITRATE"] = "0";
    REQUIRE_THROWS(hardware_factory(config, environment(values)));
    values = complete;
    values["HAND_DEVICE_NOM_TSEG1"] = "62"; // Does not produce the declared 1 Mbit/s.
    REQUIRE_THROWS(hardware_factory(config, environment(values)));
}

TEST_CASE("Hardware MiniCANFD accepts FD models and strict device channel pairs", "[comm][hardware-config]") {
    auto values = pcan_variables();
    values["HAND_DEVICE_BACKEND"] = "minicanfd";
    values["HAND_DEVICE_CHANNEL"] = "0:0";
    HandDeviceConfig config;
    for (const auto model : {Model::kL30, Model::kO30}) {
        config.model = model;
        REQUIRE_NOTHROW(hardware_factory(config, environment(values)));
    }
    for (const auto *channel : {"0", ":0", "0:", "0:0:1", "-1:0", "0:4294967296"}) {
        CAPTURE(channel);
        values["HAND_DEVICE_CHANNEL"] = channel;
        REQUIRE_THROWS(hardware_factory(config, environment(values)));
    }
    values["HAND_DEVICE_CHANNEL"] = "0:0";
    for (const auto model : {Model::kL6, Model::kO6, Model::kL20Lite, Model::kL20}) {
        config.model = model;
        config.side = HandSide::kLeft;
        REQUIRE_THROWS(hardware_factory(config, environment(values)));
    }
}

#if LINKERBOT_HAND_HAS_SLCAN && LINKERBOT_COMM_SLCAN_FD
TEST_CASE("Hardware CANable FD config supports all models and explicit Linux paths without I/O",
          "[comm][hardware-config][slcan]") {
    Variables values{
        {"HAND_DEVICE_BACKEND", "slcan"},         {"HAND_DEVICE_CHANNEL", "/dev/serial/by-id/linkerbot-test"},
        {"HAND_DEVICE_BITRATE", "1000000"},       {"HAND_DEVICE_DATA_BITRATE", "5000000"},
        {"HAND_DEVICE_SERIAL_BAUD", "460800"},    {"HAND_DEVICE_SERIAL_TIMEOUT_MS", "500"},
        {"HAND_DEVICE_SERIAL_STARTUP_MS", "100"}, {"HAND_DEVICE_SLCAN_PROTOCOL", "canable-fd"}};
    HandDeviceConfig config;
    config.side = HandSide::kLeft;
    for (const auto model : {Model::kL30, Model::kO30}) {
        config.model = model;
        REQUIRE_NOTHROW(hardware_factory(config, environment(values)));
        for (const auto *rate : {"2000000", "5000000"}) {
            auto selected = values;
            selected["HAND_DEVICE_DATA_BITRATE"] = rate;
            REQUIRE_NOTHROW(hardware_factory(config, environment(selected)));
        }
        for (const auto *key : {"HAND_DEVICE_SLCAN_PROTOCOL", "HAND_DEVICE_DATA_BITRATE"}) {
            auto selected = values;
            selected.erase(key);
            REQUIRE_THROWS(hardware_factory(config, environment(selected)));
        }
        for (const auto &[key, value] : Variables{{"HAND_DEVICE_SLCAN_PROTOCOL", "unknown"},
                                                  {"HAND_DEVICE_DATA_BITRATE", "4000000"},
                                                  {"HAND_DEVICE_SERIAL_STARTUP_MS", "5001"}}) {
            auto selected = values;
            selected[key] = value;
            REQUIRE_THROWS(hardware_factory(config, environment(selected)));
        }
    }
    values.erase("HAND_DEVICE_DATA_BITRATE");
    for (const auto model : {Model::kL6, Model::kO6, Model::kL20Lite, Model::kL20}) {
        config.model = model;
        REQUIRE_NOTHROW(hardware_factory(config, environment(values)));
    }
}
#endif
