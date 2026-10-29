#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l6/l6.hpp>

#include "hand/support/temp_path.hpp"

using namespace std::chrono_literals;
using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l6::L6;
using linkerbot::hand::l6::Side;

TEST_CASE("comm: L6 native torque and limit compensation retain read and write support", "[comm][l6][native-controls]") {
    for (const auto side : {Side::kLeft, Side::kRight}) {
        DYNAMIC_SECTION("side " << static_cast<int>(side)) {
            const uint32_t id = side == Side::kLeft ? 0x28 : 0x27;
            std::array<uint8_t, 6> torque{};
            std::array<uint8_t, 6> compensation{};
            auto transport = std::make_unique<FakeTransport>();
            auto *raw = transport.get();
            raw->on_request(id, [&](const Message &request) {
                if (request.data[0] != 0x03 && request.data[0] != 0x38) {
                    return std::vector<Message>{};
                }
                auto &values = request.data[0] == 0x03 ? torque : compensation;
                if (request.dlc == 7) {
                    std::copy_n(request.data.begin() + 1, values.size(), values.begin());
                }
                Message reply{};
                reply.id = id;
                reply.dlc = 7;
                reply.data[0] = request.data[0];
                std::copy(values.begin(), values.end(), reply.data.begin() + 1);
                return std::vector<Message>{reply};
            });
            L6 hand(side, std::move(transport), "can0", test_util::unique_mapping_path());
            const std::array<uint8_t, 6> target{0, 1, 41, 127, 200, 255};

            hand.torque.set_torques(target);
            REQUIRE(hand.torque.get_blocking(500ms).torque.to_array() == target);
            hand.limit_compensation.set_values(target, 500ms);
            REQUIRE(hand.limit_compensation.get_blocking(500ms).values == target);
            REQUIRE(hand.limit_compensation.get_snapshot().has_value());
            REQUIRE(hand.limit_compensation.get_snapshot()->values == target);

            const auto sent = raw->sent();
            REQUIRE(sent.size() == 4);
            for (size_t i = 0; i < sent.size(); ++i) {
                REQUIRE(sent[i].id == id);
                REQUIRE(sent[i].data[0] == (i < 2 ? 0x03 : 0x38));
                REQUIRE(sent[i].dlc == (i % 2 == 0 ? 7 : 1));
                if (sent[i].dlc == 7) {
                    REQUIRE(std::equal(target.begin(), target.end(), sent[i].data.begin() + 1));
                }
            }
        }
    }
}
