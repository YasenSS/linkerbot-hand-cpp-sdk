#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/api/can_configuration.hpp>
#include <linkerbot/hand/o6/hop_protocol.hpp>

using namespace linkerbot::hand;
using linkerbot::can::Message;
namespace hp = linkerbot::hand::o6::hop;
namespace wire = linkerbot::hand::protocol::hop;

namespace {
    Message reply(uint8_t mi, uint8_t si, const std::vector<uint8_t> &data, bool rts = false, bool can_fd = true) {
        Message result;
        result.id = 0x401;
        result.is_fd = can_fd;
        result.dlc = can_fd ? wire::canfd_wire_bytes(data.size() + 3) : static_cast<uint8_t>(data.size() + 3);
        result.data[0] = static_cast<uint8_t>(mi | (rts ? 0x80U : 0U));
        result.data[1] = si;
        result.data[2] = static_cast<uint8_t>(data.size());
        std::copy(data.begin(), data.end(), result.data.begin() + 3);
        return result;
    }
} // namespace

TEST_CASE("O6 HOP strict range and address vectors for CAN FD and CAN 2.0", "[protocol][o6-hop]") {
    REQUIRE(hp::response_id(0) == 0x400);
    REQUIRE(hp::response_id(0x3fe) == 0x7fe);
    REQUIRE_THROWS_AS(hp::response_id(0x3ff), ValidationError);
    REQUIRE_THROWS_AS(hp::transaction(1, 0x80, 0, 1), ValidationError);
    REQUIRE_THROWS_AS(hp::transaction(1, 1, 250, 10), ValidationError);
    REQUIRE_THROWS_AS(hp::transaction(1, 1, 0, 62), ValidationError);
    REQUIRE_THROWS_AS(hp::transaction(1, 1, 0, 6, {}, false, {}, false), ValidationError);

    auto fd_plan = hp::transaction(1, hp::kMiCurrent, 5, 10, {}, true, {}, true);
    REQUIRE(fd_plan.frames[0].dlc == 3);
    REQUIRE(fd_plan.frames[0].data[0] == 0x84);
    REQUIRE(fd_plan.frames[0].is_fd);
    REQUIRE_FALSE(fd_plan.frames[0].is_extended);
    REQUIRE_FALSE(fd_plan.frames[0].is_brs);
    auto fd_msg = reply(hp::kMiCurrent, 5, std::vector<uint8_t>(10, 9), true, true);
    REQUIRE(fd_plan.consume(fd_msg).kind == protocol::ProgressKind::complete);
    fd_msg.is_fd = false;
    REQUIRE(fd_plan.consume(fd_msg).kind == protocol::ProgressKind::unmatched);
    fd_msg.is_fd = true;
    fd_msg.data[0] = hp::kMiCurrent;
    REQUIRE(fd_plan.consume(fd_msg).kind == protocol::ProgressKind::unmatched);
    fd_msg.data[0] = 0x4f;
    fd_msg.data[2] = 1;
    REQUIRE(fd_plan.consume(fd_msg).kind == protocol::ProgressKind::unmatched);
    fd_msg.data[0] = 0x84;
    fd_msg.data[2] = 9;
    REQUIRE_THROWS_AS(fd_plan.consume(fd_msg), ProtocolError);
    fd_msg.data[2] = 10;
    fd_msg.dlc = 13;
    REQUIRE_THROWS_AS(fd_plan.consume(fd_msg), ProtocolError);
    fd_msg.dlc = 8;
    REQUIRE_THROWS_AS(fd_plan.consume(fd_msg), ProtocolError);

    auto can_plan = hp::transaction(1, hp::kMiPosition, 10, 5, {}, false, {}, false);
    REQUIRE(can_plan.frames[0].dlc == 3);
    REQUIRE(can_plan.frames[0].data[0] == hp::kMiPosition);
    REQUIRE_FALSE(can_plan.frames[0].is_fd);
    REQUIRE_FALSE(can_plan.frames[0].is_extended);
    REQUIRE_FALSE(can_plan.frames[0].is_brs);
    auto can_msg = reply(hp::kMiPosition, 10, {1, 2, 3, 4, 5}, false, false);
    REQUIRE(can_plan.consume(can_msg).kind == protocol::ProgressKind::complete);
    auto fd_mismatch = reply(hp::kMiPosition, 10, {1, 2, 3, 4, 5}, false, true);
    REQUIRE(can_plan.consume(fd_mismatch).kind == protocol::ProgressKind::unmatched);
    can_msg.dlc = 12;
    REQUIRE_THROWS_AS(can_plan.consume(can_msg), ProtocolError);
}

TEST_CASE("O6 HOP 8 bit projection and mounted write vectors", "[protocol][o6-hop]") {
    for (const bool can_fd : {true, false}) {
        const std::vector<uint8_t> canonical{10, 20, 30, 40, 50, 60};
        auto plan = hp::write_joints(1, hp::kMiPosition, canonical, false, "angle_targets", can_fd);
        REQUIRE(plan.frames.size() == 2);
        REQUIRE(plan.frames[0].is_fd == can_fd);
        REQUIRE(plan.frames[1].is_fd == can_fd);
        REQUIRE(plan.frames[0].data[1] == 5);
        REQUIRE(plan.frames[0].data[2] == 1);
        REQUIRE(plan.frames[0].dlc == 4);
        REQUIRE(plan.frames[0].data[3] == 20);
        REQUIRE(plan.frames[1].data[1] == 10);
        REQUIRE(plan.frames[1].data[2] == 5);
        REQUIRE(plan.frames[1].dlc == 8);
        REQUIRE(plan.frames[1].data[3] == 10);
        REQUIRE(plan.frames[1].data[4] == 30);
        REQUIRE(plan.frames[1].data[5] == 40);
        REQUIRE(plan.frames[1].data[6] == 50);
        REQUIRE(plan.frames[1].data[7] == 60);

        auto tail = reply(hp::kMiPosition, 10, {10, 30, 40, 50, 60}, false, can_fd);
        REQUIRE(plan.consume(tail).kind == protocol::ProgressKind::pending);
        REQUIRE(plan.consume(tail).kind == protocol::ProgressKind::pending);
        const auto result = plan.consume(reply(hp::kMiPosition, 5, {20}, false, can_fd));
        REQUIRE(result.kind == protocol::ProgressKind::complete);
        REQUIRE(hp::project_bytes(std::any_cast<std::vector<uint8_t>>(result.value)) ==
                std::array<uint8_t, 6>{10, 20, 30, 40, 50, 60});
        REQUIRE_THROWS_AS(hp::project_bytes({0}), ProtocolError);

        auto stall =
            hp::write_joints(1, hp::kMiStallThreshold, {1, 2, 3, 4, 5, 6}, true, "stall_configuration", can_fd);
        REQUIRE(stall.frames[0].data[0] == static_cast<uint8_t>(hp::kMiStallThreshold | 0x80U));
        REQUIRE(stall.consume(reply(hp::kMiStallThreshold, 5, {2}, true, can_fd)).kind ==
                protocol::ProgressKind::pending);
        REQUIRE_THROWS_AS(stall.consume(reply(hp::kMiStallThreshold, 5, {3}, true, can_fd)), ProtocolError);
    }
}

TEST_CASE("HOP CAN configuration codes and MI 0x35 framing", "[protocol][o6-hop]") {
    const uint8_t request_id[] = {0x23, 0x01};
    const uint8_t interface[] = {2, 0, 1, 13};
    const auto parsed = parse_hop_can_configuration(request_id, 2, interface, 4);
    REQUIRE(parsed.request_id == 0x123);
    REQUIRE(parsed.response_id() == 0x523);
    REQUIRE(parsed.interface_mode == HopCanInterfaceMode::kCanFd);
    REQUIRE_FALSE(parsed.brs);
    REQUIRE(parsed.nominal_bitrate == 1000000);
    REQUIRE(parsed.data_bitrate == 100000);
    REQUIRE(static_cast<uint8_t>(HopCanInterfaceMode::kCan20) == 1);
    REQUIRE(static_cast<uint8_t>(HopCanInterfaceMode::kCanFd) == 2);

    const uint8_t classic[] = {1, 1, 8, 1};
    const auto classic_config = parse_hop_can_configuration(request_id, 2, classic, 4);
    REQUIRE(classic_config.interface_mode == HopCanInterfaceMode::kCan20);
    REQUIRE(classic_config.brs);
    REQUIRE(classic_config.nominal_bitrate == 100000);
    REQUIRE(classic_config.data_bitrate == 8000000);
    REQUIRE(decode_hop_nominal_bitrate(1) == 1000000);
    REQUIRE(decode_hop_nominal_bitrate(8) == 100000);
    REQUIRE(decode_hop_data_bitrate(1) == 8000000);
    REQUIRE(decode_hop_data_bitrate(13) == 100000);

    const uint8_t too_large[] = {0xff, 0x03};
    REQUIRE_THROWS_AS(parse_hop_can_configuration(too_large, 2, interface, 4), ProtocolError);
    const uint8_t bad_mode[] = {0, 0, 1, 1};
    const uint8_t bad_mode_high[] = {3, 0, 1, 1};
    const uint8_t bad_brs[] = {2, 2, 1, 1};
    REQUIRE_THROWS_AS(parse_hop_can_configuration(request_id, 2, bad_mode, 4), ProtocolError);
    REQUIRE_THROWS_AS(parse_hop_can_configuration(request_id, 2, bad_mode_high, 4), ProtocolError);
    REQUIRE_THROWS_AS(parse_hop_can_configuration(request_id, 2, bad_brs, 4), ProtocolError);
    REQUIRE_THROWS_AS(decode_hop_nominal_bitrate(0), ProtocolError);
    REQUIRE_THROWS_AS(decode_hop_nominal_bitrate(9), ProtocolError);
    REQUIRE_THROWS_AS(decode_hop_data_bitrate(0), ProtocolError);
    REQUIRE_THROWS_AS(decode_hop_data_bitrate(14), ProtocolError);
    REQUIRE_THROWS_AS(parse_hop_can_configuration(request_id, 1, interface, 4), ProtocolError);
    REQUIRE_THROWS_AS(parse_hop_can_configuration(nullptr, 2, interface, 4), ProtocolError);

    auto request = hp::transaction(1, hp::kMiConfig, 0x00, 2, {}, true, {}, false);
    REQUIRE_FALSE(request.frames[0].is_fd);
    REQUIRE(request.frames[0].dlc <= 8);
    REQUIRE(request.frames[0].data[0] == 0xB5);
    REQUIRE(request.frames[0].data[1] == 0x00);
    REQUIRE(request.frames[0].data[2] == 0x02);
    REQUIRE(request.consume(reply(hp::kMiConfig, 0x00, {0x01, 0x00}, true, false)).kind ==
            protocol::ProgressKind::complete);

    auto interface_read = hp::transaction(1, hp::kMiConfig, 0x06, 4, {}, true, {}, false);
    REQUIRE_FALSE(interface_read.frames[0].is_fd);
    REQUIRE(interface_read.frames[0].dlc <= 8);
    REQUIRE(interface_read.frames[0].data[0] == 0xB5);
    REQUIRE(interface_read.frames[0].data[1] == 0x06);
    REQUIRE(interface_read.frames[0].data[2] == 0x04);

    auto mode = hp::transaction(1, hp::kMiConfig, 0x06, 1, {1}, false, {}, false);
    REQUIRE_FALSE(mode.frames[0].is_fd);
    REQUIRE(mode.frames[0].dlc == 4);
    REQUIRE(mode.frames[0].data[0] == 0x35);
    REQUIRE(mode.frames[0].data[1] == 0x06);
    REQUIRE(mode.frames[0].data[2] == 0x01);
    REQUIRE(mode.frames[0].data[3] == 0x01);

    auto save = hp::transaction(1, hp::kMiConfig, 0x1E, 1, {1}, false, {}, true);
    REQUIRE(save.frames[0].is_fd);
    REQUIRE(save.frames[0].data[0] == 0x35);
    REQUIRE(save.frames[0].data[1] == 0x1E);
    REQUIRE(save.frames[0].data[2] == 0x01);
    REQUIRE(save.frames[0].data[3] == 0x01);
    REQUIRE(save.consume(reply(hp::kMiConfig, 0x1E, {0x00}, false, true)).kind == protocol::ProgressKind::complete);
}
