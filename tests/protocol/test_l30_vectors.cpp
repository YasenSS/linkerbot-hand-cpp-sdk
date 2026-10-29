#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/l30/protocol_backend.hpp>
using namespace linkerbot::hand::l30;
namespace common = linkerbot::hand::protocol;
namespace {
    linkerbot::can::Message response(const common::TransactionPlan &plan, std::vector<uint8_t> bytes) {
        linkerbot::can::Message m;
        m.id = protocol::expected_response_id(plan.frames.front().id);
        m.is_extended = true;
        m.is_fd = true;
        m.dlc = bytes.size();
        std::copy(bytes.begin(), bytes.end(), m.data.begin());
        return m;
    }
    linkerbot::can::Message fragment(const common::TransactionPlan &plan, bool first) {
        const auto count = first ? protocol::kTactileFirstLength : protocol::kTactileSecondLength;
        std::vector<uint8_t> bytes(count + 3, first ? 12 : 34);
        bytes[0] = count;
        bytes[1] = first ? 0x10 : 0x11;
        bytes[2] = 0;
        return response(plan, bytes);
    }
}
TEST_CASE("L30 production ACK/read plans retain raw status and reject invalid frames", "[protocol][l30]") {
    REQUIRE(protocol::kSubCurrentLimit == 0x02);
    REQUIRE(protocol::kSubCurrent == 0x02);
    auto plan =
        protocol::request_plan(protocol::request_frame(1, 0, protocol::kParentControl, protocol::kSubEnable,
                                                       protocol::kAccessWrite, protocol::empty_request_payload(), 2));
    REQUIRE(plan.frames.front().id == 0x0220E100);
    REQUIRE(plan.publish_after_send);
    auto ack = response(plan, {0, 0, 0x22});
    for (int fault = 0; fault < 6; ++fault) {
        auto bad = ack;
        switch (fault) {
        case 0:
            bad.is_extended = false;
            break;
        case 1:
            bad.is_fd = false;
            break;
        case 2:
            bad.dlc = 65;
            break;
        case 3:
            bad.dlc = 2;
            break;
        case 4:
            bad.data[0] = 63;
            break;
        case 5:
            bad.data[1] = 0x10;
            break;
        }
        REQUIRE(plan.consume(bad).kind != common::ProgressKind::complete);
    }
    auto result = plan.consume(ack);
    REQUIRE(result.kind == common::ProgressKind::complete);
    REQUIRE(std::any_cast<std::vector<uint8_t>>(result.value) == std::vector<uint8_t>{0, 0, 0x22});
}
TEST_CASE("L30 tactile assemblers are local and support reverse order and duplicates", "[protocol][l30]") {
    auto a = protocol::tactile_plan(1, 0, 1), b = protocol::tactile_plan(1, 0, 1);
    auto first = fragment(a, true), second = fragment(a, false);
    REQUIRE(a.consume(second).kind == common::ProgressKind::pending);
    REQUIRE(a.consume(second).duplicates == 1);
    REQUIRE(b.consume(first).kind == common::ProgressKind::pending);
    const auto result = a.consume(first);
    REQUIRE(result.kind == common::ProgressKind::complete);
    const auto matrix = std::any_cast<std::array<uint8_t, 72>>(result.value);
    for (size_t i = 0; i < matrix.size(); ++i) {
        REQUIRE(matrix[i] == (i < 61 ? 12 : 34));
    }
    REQUIRE(b.consume(second).kind == common::ProgressKind::complete);
}
TEST_CASE("L30 tactile malformed and conflicting fragments fail without partial value", "[protocol][l30]") {
    for (int fault = 0; fault < 4; ++fault) {
        auto plan = protocol::tactile_plan(1, 0, 1);
        auto first = fragment(plan, true);
        REQUIRE(plan.consume(first).kind == common::ProgressKind::pending);
        switch (fault) {
        case 0:
            first.data[3] ^= 1;
            break;
        case 1:
            first.data[0] = 60;
            break;
        case 2:
            first.dlc = 63;
            break;
        case 3:
            first.data[1] = 0x12;
            break;
        }
        auto result = plan.consume(first);
        REQUIRE(result.kind == common::ProgressKind::failed);
        REQUIRE_FALSE(result.value.has_value());
        REQUIRE_FALSE(result.terminal_error);
        REQUIRE_THROWS_AS(std::rethrow_exception(result.error), protocol::ProtocolError);
        if (!fault) {
            REQUIRE(result.conflicting_duplicates == 1);
        }
    }
}
TEST_CASE("L30 tactile normal error ACK retains status without late-fragment poison", "[protocol][l30]") {
    auto plan = protocol::tactile_plan(1, 0, 1);
    auto result = plan.consume(response(plan, {0, 0, 0x21}));
    REQUIRE(result.kind == common::ProgressKind::failed);
    REQUIRE(result.terminal_error);
    try {
        std::rethrow_exception(result.error);
    } catch (const protocol::ProtocolError &e) {
        REQUIRE(e.status() == 0x21);
    }
}
TEST_CASE("L30 periodic decoder distinguishes reports from config ACKs and other nodes", "[protocol][l30]") {
    auto msg = protocol::build_message({0, 0, protocol::kParentPeriodic, 1, 0, 1}, {1, 0, 7}, 3);
    REQUIRE(protocol::decode_report(msg, 1, 0).kind == common::ProgressKind::complete);
    auto report = std::any_cast<protocol::Report>(protocol::decode_report(msg, 1, 0).value);
    REQUIRE(report.subcmd == 1);
    REQUIRE(report.payload == std::vector<uint8_t>{1, 0, 7});
    REQUIRE(protocol::decode_report(msg, 2, 0).kind == common::ProgressKind::unmatched);
    msg.id |= (1u << 25);
    REQUIRE(protocol::decode_report(msg, 1, 0).kind == common::ProgressKind::unmatched);
}
