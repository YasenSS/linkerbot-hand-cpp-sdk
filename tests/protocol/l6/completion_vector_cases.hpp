#include <stdexcept>
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/l6/protocol_backend.hpp>
#ifndef L6_COMPLETION_TAGS
#define L6_COMPLETION_TAGS "[protocol][l6][completion]"
#endif
using namespace linkerbot::hand::l6;
namespace pure = linkerbot::hand::protocol;
namespace {
    struct SerialArguments {
        uint8_t marker;
        uint8_t dlc = 8;
    };
    linkerbot::can::Message serial(const SerialArguments &arguments) {
        auto m = protocol::frame(0x28, kSerialOpcode, std::vector<uint8_t>(7, 'A'));
        m.data[1] = arguments.marker;
        m.dlc = arguments.dlc;
        return m;
    }
}
TEST_CASE("L6 serial never pads a short fragment into a complete identity", L6_COMPLETION_TAGS) {
    for (uint8_t dlc = 3; dlc < 8; ++dlc) {
        auto plan = protocol::read_serial(0x28);
        REQUIRE(plan.consume(serial({0, dlc})).kind != pure::ProgressKind::complete);
        REQUIRE(plan.consume(serial({1})).kind != pure::ProgressKind::complete);
        REQUIRE(plan.consume(serial({2})).kind != pure::ProgressKind::complete);
        REQUIRE(plan.consume(serial({3})).kind != pure::ProgressKind::complete);
        auto done = plan.consume(serial({0}));
        REQUIRE(done.kind == pure::ProgressKind::complete);
        REQUIRE(std::any_cast<std::string>(done.value).size() == 24);
    }
}
TEST_CASE("L6 serial selects one encoding per transaction and tolerates valid reordering", L6_COMPLETION_TAGS) {
    for (bool offsets_first : {false, true}) {
        auto plan = protocol::read_serial(0x28);
        REQUIRE(plan.consume(serial({0})).kind == pure::ProgressKind::pending);
        REQUIRE(plan.consume(serial({static_cast<uint8_t>(offsets_first ? 6 : 1)})).kind ==
                pure::ProgressKind::pending);
        REQUIRE_THROWS(plan.consume(serial({static_cast<uint8_t>(offsets_first ? 2 : 12)})));
    }
    for (bool offsets : {false, true}) {
        auto a = protocol::read_serial(0x28), b = protocol::read_serial(0x28);
        if (!offsets) {
            REQUIRE(a.consume(serial({3})).kind == pure::ProgressKind::pending);
        }
        REQUIRE(a.consume(serial({static_cast<uint8_t>(offsets ? 12 : 2)})).kind == pure::ProgressKind::pending);
        REQUIRE(a.consume(serial({static_cast<uint8_t>(offsets ? 6 : 1)})).kind == pure::ProgressKind::pending);
        REQUIRE(b.consume(serial({0})).kind == pure::ProgressKind::pending);
        REQUIRE(a.consume(serial({static_cast<uint8_t>(offsets ? 6 : 1)})).kind == pure::ProgressKind::pending);
        REQUIRE(a.consume(serial({0})).kind == pure::ProgressKind::complete);
        REQUIRE(b.consume(serial({static_cast<uint8_t>(offsets ? 6 : 1)})).kind == pure::ProgressKind::pending);
    }
}
TEST_CASE("L6 scalar protocol domains reject wrong envelope and exact lengths", L6_COMPLETION_TAGS) {
    for (uint8_t op : {kTorqueOpcode, kCurrentOpcode, kTemperatureOpcode, kFaultOpcode, kStallTimeOpcode,
                       kStallThresholdOpcode, kStallTorqueOpcode}) {
        auto make = [&] {
            return op >= kStallTimeOpcode ? protocol::read_stall(0x28, op) : protocol::read_telemetry(0x28, op);
        };
        auto m = protocol::frame(0x28, op, std::vector<uint8_t>(6, 1));
        for (int error = 0; error < 7; ++error) {
            auto bad = m;
            switch (error) {
            case 0:
                bad.id++;
                break;
            case 1:
                bad.is_extended = true;
                break;
            case 2:
                bad.is_fd = true;
                break;
            case 3:
                bad.is_brs = true;
                break;
            case 4:
                bad.dlc--;
                break;
            case 5:
                bad.dlc++;
                break;
            case 6:
                bad.data[0] = 0;
                break;
            default:
                throw std::logic_error("invalid test vector selector");
            }
            REQUIRE(make().consume(bad).kind == pure::ProgressKind::unmatched);
        }
        REQUIRE(make().consume(m).kind == pure::ProgressKind::complete);
    }
}
TEST_CASE("L6 version config and command vectors retain wire contracts", L6_COMPLETION_TAGS) {
    for (uint8_t op : {kPcbVersionOpcode, kFirmwareVersionOpcode, kMechanicalVersionOpcode}) {
        auto plan = protocol::read_version(0x28, op);
        auto msg = protocol::frame(
            0x28, op, op == kPcbVersionOpcode ? std::vector<uint8_t>{1, 2, 3, 4} : std::vector<uint8_t>{2, 3, 4});
        auto done = plan.consume(msg);
        REQUIRE(done.kind == pure::ProgressKind::complete);
        REQUIRE(std::any_cast<Version>(done.value).major == 2);
        --msg.dlc;
        REQUIRE(plan.consume(msg).kind == pure::ProgressKind::unmatched);
    }
    for (uint8_t sub : {1, 2}) {
        auto plan = protocol::set_id(0x28, sub, 0x29);
        REQUIRE(plan.frames.front().dlc == 3);
        REQUIRE(plan.frames.front().data[1] == sub);
        REQUIRE(plan.frames.front().data[2] == 0x29);
        auto msg = protocol::frame(0x28, kSetIdOpcode, {0x29, 0x28});
        REQUIRE(plan.consume(msg).kind == pure::ProgressKind::complete);
        msg.is_brs = true;
        REQUIRE(plan.consume(msg).kind == pure::ProgressKind::unmatched);
    }
    for (uint8_t op : {uint8_t{0xCC}, uint8_t{0xCE}, uint8_t{0xCF}}) {
        auto p = protocol::filled_command(0x28, op, op, 7);
        REQUIRE_FALSE(p.consume);
        REQUIRE(p.frames.front().dlc == 7);
        for (size_t i = 0; i < 7; ++i) {
            REQUIRE(p.frames.front().data[i] == op);
        }
    }
    auto angle = protocol::angle(0x28, std::array<uint8_t, 6>{1, 2, 3, 4, 5, 6}, false, std::chrono::milliseconds{5});
    REQUIRE_FALSE(angle.consume);
    REQUIRE(angle.frames.front().dlc == 7);
    REQUIRE(angle.frames.front().data[6] == 6);
    auto optional = protocol::angle(0x28, std::array<uint8_t, 6>{}, true, std::chrono::milliseconds{5},
                                    std::chrono::microseconds{5000});
    REQUIRE(optional.optional_response_window == std::chrono::microseconds{5000});
    REQUIRE_FALSE(optional.poison_on_timeout);
}
