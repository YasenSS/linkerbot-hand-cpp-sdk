#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/o30/transaction.hpp>
#include <linkerbot/hand/o6/protocol_backend.hpp>

TEST_CASE("protocol: O6 angle bytes are independent classic vectors", "[protocol]") {
    namespace p = linkerbot::hand::o6::protocol;
    const auto read = p::read_angle(0x28);
    REQUIRE(read.frames.size() == 1);
    REQUIRE(read.frames[0].id == 0x28);
    REQUIRE(read.frames[0].dlc == 1);
    REQUIRE(read.frames[0].data[0] == 0x01);
    linkerbot::can::Message wire;
    wire.id = 0x28;
    wire.dlc = 7;
    wire.data = {0x01, 0x00, 0x33, 0x66, 0x99, 0xcc, 0xff};
    using Kind = linkerbot::hand::protocol::ProgressKind;
    REQUIRE(read.consume(wire).kind == Kind::complete);
    wire.is_extended = true;
    REQUIRE(read.consume(wire).kind == Kind::unmatched);
    wire.is_extended = false;
    wire.is_fd = true;
    REQUIRE(read.consume(wire).kind == Kind::unmatched);
    wire.is_fd = false;
    wire.dlc = 6;
    REQUIRE(read.consume(wire).kind == Kind::unmatched);
}
TEST_CASE("protocol: HOP range has full coverage and independent RTS matching", "[protocol]") {
    using linkerbot::hand::o30::protocol::RangeResponse;
    using Kind = linkerbot::hand::protocol::ProgressKind;
    linkerbot::can::Message tail;
    tail.id = 0x401;
    tail.is_fd = true;
    tail.dlc = 16;
    tail.data = {0x11, 61, 11, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0};
    RangeResponse assembler(1, 0x11, 0, 72, false, false);
    REQUIRE(assembler.consume(tail).kind == Kind::pending);
    auto wrong_rts = tail;
    wrong_rts.data[0] = 0x91;
    REQUIRE(assembler.consume(wrong_rts).kind == Kind::unmatched);
    auto classic = tail;
    classic.is_fd = false;
    REQUIRE(assembler.consume(classic).kind == Kind::unmatched);
    linkerbot::can::Message head;
    head.id = 0x401;
    head.is_fd = true;
    head.dlc = 64;
    head.data[0] = 0x11;
    head.data[1] = 0;
    head.data[2] = 61;
    for (size_t i = 0; i < 61; ++i) {
        head.data[i + 3] = static_cast<uint8_t>(100 + i);
    }
    auto complete = assembler.consume(head);
    REQUIRE(complete.kind == Kind::complete);
    const auto bytes = std::any_cast<std::vector<uint8_t>>(complete.value);
    REQUIRE(bytes.size() == 72);
    REQUIRE(bytes[0] == 100);
    REQUIRE(bytes[60] == 160);
    REQUIRE(bytes[61] == 1);
    REQUIRE(bytes[71] == 11);
}
TEST_CASE("protocol: HOP conflicting duplicate and short object are errors", "[protocol]") {
    using namespace linkerbot::hand::o30::protocol;
    linkerbot::can::Message tail;
    tail.id = 0x401;
    tail.is_fd = true;
    tail.dlc = 16;
    tail.data = {0x11, 61, 11, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    RangeResponse assembler(1, 0x11, 0, 72, false, false);
    (void)assembler.consume(tail);
    tail.data[3] = 99;
    REQUIRE_THROWS_AS(assembler.consume(tail), ProtocolError);
    RangeResponse short_read(1, 1, 0, 36, false, false);
    linkerbot::can::Message short_frame;
    short_frame.id = 0x401;
    short_frame.is_fd = true;
    short_frame.dlc = 4;
    short_frame.data = {1, 0, 1, 42};
    REQUIRE_THROWS_AS(short_read.consume(short_frame), ProtocolError);
}

#include <linkerbot/hand/l6/protocol_backend.hpp>
TEST_CASE("L6 current limit torque and measured current use distinct raw opcodes", "[protocol][l6]") {
    namespace l6 = linkerbot::hand::l6;
    const auto current_limit = l6::protocol::read_telemetry(0x28, l6::kCurrentLimitOpcode);
    const auto torque = l6::protocol::read_telemetry(0x28, l6::kTorqueOpcode);
    const auto current = l6::protocol::read_telemetry(0x28, l6::kCurrentOpcode);
    REQUIRE(current_limit.frames[0].data[0] == 0x02);
    REQUIRE(torque.frames[0].data[0] == 0x03);
    REQUIRE(current.frames[0].data[0] == 0x36);

    linkerbot::can::Message response{};
    response.id = 0x28;
    response.dlc = 7;
    response.data = {0x36, 0, 51, 102, 153, 204, 255};
    auto decoded = l6::protocol::telemetry(0x28, 0x36, response);
    REQUIRE(decoded.kind == linkerbot::hand::protocol::ProgressKind::complete);
    auto data = std::any_cast<l6::CurrentData>(decoded.value);
    REQUIRE(data.current.to_array()[1] == 51);
    REQUIRE(data.current.to_array()[5] == 255);

    response.data[0] = 0x02;
    const auto limit_data = std::any_cast<l6::CurrentLimitData>(l6::protocol::telemetry(0x28, 0x02, response).value);
    REQUIRE(limit_data.current_limit.to_array()[5] == 255);
    response.data[0] = 0x03;
    const auto torque_data = std::any_cast<l6::TorqueData>(l6::protocol::telemetry(0x28, 0x03, response).value);
    REQUIRE(torque_data.torque.to_array()[5] == 255);
    response.is_fd = true;
    REQUIRE(l6::protocol::telemetry(0x28, 0x03, response).kind == linkerbot::hand::protocol::ProgressKind::unmatched);
}
TEST_CASE("L6 serial fragments assemble only inside the requested transaction", "[protocol][l6]") {
    auto plan = linkerbot::hand::l6::protocol::read_serial(0x28);
    linkerbot::can::Message reply{};
    reply.id = 0x28;
    reply.dlc = 8;
    reply.data = {0xc0, 2, 'C', 'C', 'C', 'C', 'C', 'C'};
    REQUIRE(plan.consume(reply).kind == linkerbot::hand::protocol::ProgressKind::pending);
    reply.data = {0xc0, 0, 'A', 'A', 'A', 'A', 'A', 'A'};
    REQUIRE(plan.consume(reply).kind == linkerbot::hand::protocol::ProgressKind::pending);
    reply.data = {0xc0, 1, 'B', 'B', 'B', 'B', 'B', 'B'};
    REQUIRE(plan.consume(reply).kind == linkerbot::hand::protocol::ProgressKind::pending);
    reply.data = {0xc0, 3, 'D', 'D', 'D', 'D', 'D', 'D'};
    REQUIRE(std::any_cast<std::string>(plan.consume(reply).value) == "AAAAAABBBBBBCCCCCCDDDDDD");
    auto another = linkerbot::hand::l6::protocol::read_serial(0x28);
    REQUIRE(another.consume(reply).kind == linkerbot::hand::protocol::ProgressKind::pending);
    reply.data[3] = 'X';
    REQUIRE_THROWS(another.consume(reply));
}

TEST_CASE("protocol: negative HOP ACK is terminal while corrupt fragments isolate", "[protocol][o30]") {
    namespace p = linkerbot::hand::o30::protocol;
    using Kind = linkerbot::hand::protocol::ProgressKind;
    auto plan = p::transaction(1, 1, 0, 36, {}, false);
    linkerbot::can::Message negative{};
    negative.id = 0x401;
    negative.is_fd = true;
    negative.dlc = 4;
    negative.data = {0x4f, 0, 1, 2};
    const auto error = plan.consume(negative);
    REQUIRE(error.kind == Kind::failed);
    REQUIRE(error.terminal_error);
    REQUIRE_THROWS_AS(std::rethrow_exception(error.error), p::ProtocolError);
    auto malformed = p::transaction(1, 1, 0, 36, {}, false);
    auto short_reply = negative;
    short_reply.data = {1, 0, 1, 42};
    const auto failure = malformed.consume(short_reply);
    REQUIRE(failure.kind == Kind::failed);
    REQUIRE_FALSE(failure.terminal_error);
}

TEST_CASE("protocol: L6 tactile row validation and conflicting duplicates", "[protocol][l6]") {
    auto plan = linkerbot::hand::l6::protocol::read_force(0x28, 0xb1, std::chrono::milliseconds{5});
    linkerbot::can::Message row{};
    row.id = 0x28;
    row.dlc = 8;
    row.data = {0xb1, 0x30, 1, 2, 3, 4, 5, 6};
    using Kind = linkerbot::hand::protocol::ProgressKind;
    REQUIRE(plan.consume(row).kind == Kind::pending);
    REQUIRE(plan.consume(row).duplicates == 1);
    row.data[2] = 7;
    const auto conflict = plan.consume(row);
    REQUIRE(conflict.kind == Kind::failed);
    REQUIRE(conflict.conflicting_duplicates == 1);
    auto malformed = linkerbot::hand::l6::protocol::read_force(0x28, 0xb1, std::chrono::milliseconds{5});
    row.data[1] = 0x31;
    REQUIRE(malformed.consume(row).kind == Kind::failed);
}
