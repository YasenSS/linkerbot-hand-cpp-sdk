#include <catch2/catch_test_macros.hpp>
#include <type_traits>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/identity.hpp>
#include <linkerbot/hand/l20/version_manager.hpp>
#include <linkerbot/hand/l20lite/version_manager.hpp>
#include <linkerbot/hand/l6/version_manager.hpp>
#include <linkerbot/hand/o6/version_manager.hpp>

using namespace linkerbot::hand;

namespace {
    template <typename Manager> void verify_classic(const std::string &code, bool offsets = false, uint32_t id = 0x28) {
        auto transport = std::make_unique<linkerbot::can::FakeTransport>();
        transport->on_request(id, [=](const linkerbot::can::Message &req) {
            std::vector<linkerbot::can::Message> replies;
            if (req.data[0] == 0xC0) {
                for (int chunk = 0; chunk < (offsets ? 3 : 4); ++chunk) {
                    linkerbot::can::Message msg;
                    msg.id = id;
                    msg.dlc = 8;
                    msg.data[0] = 0xC0;
                    msg.data[1] = static_cast<uint8_t>(offsets ? chunk * 6 : chunk);
                    for (size_t i = 0; i < 6; ++i) {
                        const auto index = static_cast<size_t>(chunk) * 6 + i;
                        msg.data[2 + i] = index < code.size() ? code[index] : 0;
                    }
                    replies.push_back(msg);
                }
            } else {
                linkerbot::can::Message msg;
                msg.id = id;
                // L6 PCB replies carry a fourth version byte; the other
                // classic profiles use exactly [command, major, minor, patch].
                msg.dlc = req.data[0] == 0xC1 && std::is_same_v<Manager, l6::VersionManager> ? 5 : 4;
                msg.data[0] = req.data[0];
                msg.data[1] = 1;
                replies.push_back(msg);
            }
            return replies;
        });
        linkerbot::can::CANDispatcher dispatcher(std::move(transport));
        Manager manager(id, dispatcher);
        manager.start();
        const auto info = manager.get_verified_device_info(std::chrono::milliseconds{300});
        REQUIRE(info.serial_number == code);
    }
}

// Python 出厂型号别名及侧别规则； malformed 与 mismatch 分开报告。
TEST_CASE("comm: factory identity matches Python model and side rules", "[comm][identity]") {
    const auto identity = parse_factory_identity(" lhl6 -03-001-l-Z-1-A ", {"LHL6"}, IdentitySide::kLeft);
    REQUIRE(identity.serial_number == "001");
    REQUIRE_THROWS_AS(parse_factory_identity("LHO6-1-1-L", {"LHL6"}, IdentitySide::kLeft), IdentityMismatchError);
    REQUIRE_THROWS_AS(parse_factory_identity("LHL6-1-1-R", {"LHL6"}, IdentitySide::kLeft), IdentityMismatchError);
    REQUIRE_THROWS_AS(parse_factory_identity("LHL6-1", {"LHL6"}, IdentitySide::kLeft), IdentityProtocolError);
    REQUIRE_THROWS_AS(parse_factory_identity("LHL6-1-1-X", {"LHL6"}, IdentitySide::kLeft), IdentityProtocolError);
    REQUIRE_THROWS_AS(parse_factory_identity("LHL6--1-L", {"LHL6"}, IdentitySide::kLeft), IdentityProtocolError);
}

// 四种经典 CAN 的真实版本管理器都接入可选校验，不改变原始串号字段。
TEST_CASE("comm: classic version managers verify identity", "[comm][identity]") {
    SECTION("L6") {
        verify_classic<l6::VersionManager>("LHL6-1-123-L");
        REQUIRE_THROWS_AS(verify_classic<l6::VersionManager>("LHO6-1-123-L"), IdentityMismatchError);
    }
    SECTION("O6") {
        verify_classic<o6::VersionManager>("LHO6-1-123-R", false, 0x27);
        REQUIRE_THROWS_AS(verify_classic<o6::VersionManager>("LHO6-1-123-R"), IdentityMismatchError);
    }
    SECTION("L20 aliases") {
        for (const auto *model : {"L20", "LHT20", "L25", "LHL25", "LHG20"}) {
            verify_classic<l20::VersionManager>(std::string(model) + "-1-123-L");
        }
        REQUIRE_THROWS_AS(verify_classic<l20::VersionManager>("LHT10-1-123-L"), IdentityMismatchError);
    }
    SECTION("L20Lite offsets") {
        verify_classic<l20lite::VersionManager>("LHT10-1-123-L", true);
        REQUIRE_THROWS_AS(verify_classic<l20lite::VersionManager>("LHT20-1-123-L", true), IdentityMismatchError);
    }
}
