#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/l6/hop_protocol.hpp>
using namespace linkerbot::hand;
using linkerbot::can::Message;
namespace hp = linkerbot::hand::l6::hop;
namespace wire = linkerbot::hand::protocol::hop;
namespace {
    Message reply(uint8_t mi, uint8_t si, std::vector<uint8_t> data, bool rts = false) {
        Message result;
        result.id = 0x401;
        result.is_fd = true;
        result.dlc = wire::canfd_wire_bytes(data.size() + 3);
        result.data[0] = mi | (rts ? 0x80 : 0);
        result.data[1] = si;
        result.data[2] = data.size();
        std::copy(data.begin(), data.end(), result.data.begin() + 3);
        return result;
    }
}
TEST_CASE("L6 HOP strict range and address vectors", "[protocol][l6-hop]") {
    REQUIRE(hp::response_id(0) == 0x400);
    REQUIRE(hp::response_id(0x3fe) == 0x7fe);
    REQUIRE_THROWS_AS(hp::response_id(0x3ff), ValidationError);
    REQUIRE_THROWS_AS(hp::transaction(1, 0x80, 0, 1), ValidationError);
    REQUIRE_THROWS_AS(hp::transaction(1, 1, 250, 10), ValidationError);
    REQUIRE_THROWS_AS(hp::transaction(1, 1, 0, 62), ValidationError);
    auto plan = hp::transaction(1, 4, 5, 10, {}, true);
    REQUIRE(plan.frames[0].dlc == 3);
    REQUIRE(plan.frames[0].data[0] == 0x84);
    REQUIRE(plan.frames[0].is_fd);
    REQUIRE_FALSE(plan.frames[0].is_extended);
    REQUIRE_FALSE(plan.frames[0].is_brs);
    auto msg = reply(4, 5, std::vector<uint8_t>(10, 9), true);
    REQUIRE(plan.consume(msg).kind == protocol::ProgressKind::complete);
    msg.data[0] = 4;
    REQUIRE(plan.consume(msg).kind == protocol::ProgressKind::unmatched);
    msg.data[0] = 0x4f;
    msg.data[2] = 1;
    REQUIRE(plan.consume(msg).kind == protocol::ProgressKind::unmatched);
    msg.data[0] = 0x84;
    msg.data[2] = 9;
    REQUIRE_THROWS_AS(plan.consume(msg), ProtocolError);
    msg.data[2] = 10;
    msg.dlc = 13;
    REQUIRE_THROWS_AS(plan.consume(msg), ProtocolError);
    msg.dlc = 8;
    REQUIRE_THROWS_AS(plan.consume(msg), ProtocolError);
}
TEST_CASE("L6 HOP 16 bit projection and mounted write vectors", "[protocol][l6-hop]") {
    const std::vector<uint8_t> canonical{0, 1, 0, 128, 255, 255, 0, 0, 1, 0, 1, 2};
    auto plan = hp::write_joints(1, 0x20, canonical, true, false, "angle_targets");
    REQUIRE(plan.frames.size() == 2);
    REQUIRE(plan.frames[0].data[1] == 10);
    REQUIRE(plan.frames[0].data[2] == 2);
    REQUIRE(plan.frames[0].data[3] == 0);
    REQUIRE(plan.frames[0].data[4] == 128);
    REQUIRE(plan.frames[1].data[1] == 20);
    REQUIRE(plan.frames[1].data[2] == 10);
    REQUIRE(plan.frames[1].dlc == 16);
    REQUIRE(plan.frames[1].data[3] == 0);
    REQUIRE(plan.frames[1].data[4] == 1);
    REQUIRE(plan.frames[1].data[13] == 0);
    auto tail = reply(0x20, 20, {0, 1, 255, 255, 0, 0, 1, 0, 1, 2});
    REQUIRE(plan.consume(tail).kind == protocol::ProgressKind::pending);
    REQUIRE(plan.consume(tail).kind == protocol::ProgressKind::pending);
    const auto result = plan.consume(reply(0x20, 10, {0, 128}));
    REQUIRE(result.kind == protocol::ProgressKind::complete);
    REQUIRE(hp::project_positions(std::any_cast<std::vector<uint8_t>>(result.value)) ==
            std::array<uint16_t, 6>{256, 32768, 65535, 0, 1, 513});
    REQUIRE_THROWS_AS(hp::project_positions({0}), ProtocolError);
    auto other = hp::write_joints(1, 2, {1, 2, 3, 4, 5, 6}, false, true, "speed_targets");
    REQUIRE(other.frames[0].data[1] == 5);
    REQUIRE(other.frames[1].data[1] == 10);
    REQUIRE(other.frames[0].data[0] == 0x82);
    REQUIRE(other.consume(reply(2, 5, {2}, true)).kind == protocol::ProgressKind::pending);
    REQUIRE_THROWS_AS(other.consume(reply(2, 5, {3}, true)), ProtocolError);
}
