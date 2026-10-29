#include <catch2/catch_test_macros.hpp>

#include "hand/support/temp_path.hpp"
#include <algorithm>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20/angle_manager.hpp>
#include <linkerbot/hand/l20lite/angle_manager.hpp>
#include <linkerbot/hand/l6/angle_manager.hpp>
#include <linkerbot/hand/o6/angle_manager.hpp>

namespace {
    template <typename Manager, size_t N>
    void check_wire_bytes(const std::array<uint8_t, N> &values, const std::vector<std::vector<uint8_t>> &expected) {
        auto transport = std::make_unique<linkerbot::can::FakeTransport>();
        auto *raw = transport.get();
        linkerbot::can::CANDispatcher dispatcher(std::move(transport));
        Manager manager(0x28, dispatcher, test_util::unique_mapping_path(), "left", "fake");
        auto mapping = manager.get_default_angle_mapping();
        for (auto &row : mapping) {
            std::reverse(row.begin(), row.end());
        }
        manager.set_angle_mapping(mapping);
        manager.set_angles(values);
        const auto sent = raw->sent();
        REQUIRE(sent.size() == expected.size());
        for (size_t i = 0; i < sent.size(); ++i) {
            REQUIRE(sent[i].id == 0x28);
            REQUIRE(std::vector<uint8_t>(sent[i].data.begin(), sent[i].data.begin() + sent[i].dlc) == expected[i]);
        }
        REQUIRE(manager.get_angle_mapping() == mapping);
    }
}

// 反向 LUT 下直接写设备字节仍逐字节相等，保留槽不受 LUT 影响。
TEST_CASE("comm: device raw angles bypass LUT for all classic hands", "[comm][angle][wire]") {
    using namespace linkerbot::hand;
    SECTION("L6") {
        check_wire_bytes<l6::AngleManager>(std::array<uint8_t, 6>{0, 1, 2, 3, 4, 255}, {{0x01, 0, 1, 2, 3, 4, 255}});
    }
    SECTION("O6") {
        check_wire_bytes<o6::AngleManager>(std::array<uint8_t, 6>{0, 1, 2, 3, 4, 255}, {{0x01, 0, 1, 2, 3, 4, 255}});
    }
    SECTION("L20Lite") {
        check_wire_bytes<l20lite::AngleManager>(std::array<uint8_t, 10>{0, 1, 2, 3, 4, 5, 6, 7, 8, 255},
                                                {{0x01, 0, 1, 2, 3, 4, 5}, {0x04, 6, 7, 8, 255}});
    }
    SECTION("L20") {
        check_wire_bytes<l20::AngleManager>(
            std::array<uint8_t, 16>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 255},
            {{0x41, 0, 1, 2, 0, 0, 3},
             {0x42, 4, 0, 5, 0, 0, 6},
             {0x43, 7, 0, 8, 0, 0, 9},
             {0x44, 10, 0, 11, 0, 0, 12},
             {0x45, 13, 0, 14, 0, 0, 255}});
    }
}
