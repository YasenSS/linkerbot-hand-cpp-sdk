#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/o6/protocol_backend.hpp>
using namespace linkerbot::hand;
using linkerbot::can::Message;
using protocol::ProgressKind;

TEST_CASE("O6 protocol scalar domains require exact classic frames", "[protocol][o6]") {
    o6::protocol::Backend backend(0x28);
    for (const auto *domain : {"angle", "current", "speed", "acceleration", "temperature", "fault", "stall_time",
                               "stall_threshold", "stall_torque"}) {
        INFO(domain);
        auto plan = backend.make_read(domain);
        REQUIRE(plan.frames.size() == 1);
        REQUIRE(plan.frames[0].dlc == 1);
        auto reply = plan.frames[0];
        reply.dlc = 7;
        REQUIRE(plan.consume(reply).kind == ProgressKind::complete);
        for (int mode = 0; mode != 6; ++mode) {
            auto bad = reply;
            if (mode == 0) {
                bad.dlc = 6;
            }
            if (mode == 1) {
                bad.dlc = 8;
            }
            if (mode == 2) {
                bad.is_fd = true;
            }
            if (mode == 3) {
                bad.is_brs = true;
            }
            if (mode == 4) {
                bad.is_extended = true;
            }
            if (mode == 5) {
                bad.id = 0x27;
            }
            REQUIRE(plan.consume(bad).kind == ProgressKind::unmatched);
        }
    }
    for (const auto *domain : {"version_pcb", "version_firmware", "version_mechanical"}) {
        auto plan = backend.make_read(domain);
        auto frame = plan.frames[0];
        frame.dlc = 4;
        REQUIRE(plan.consume(frame).kind == ProgressKind::complete);
        frame.dlc = 5;
        REQUIRE(plan.consume(frame).kind == ProgressKind::unmatched);
        frame.dlc = 3;
        REQUIRE(plan.consume(frame).kind == ProgressKind::unmatched);
    }
}

TEST_CASE("O6 angle command feedback is explicitly sourced", "[protocol][o6][control]") {
    const std::array<uint8_t, 6> target{1, 2, 3, 4, 5, 6};
    auto plan = o6::protocol::command_angle_feedback(0x28, target);
    REQUIRE(plan.frames.size() == 1);
    REQUIRE(plan.frames[0].dlc == 7);
    REQUIRE(plan.frames[0].data[1] == 1);
    auto reply = plan.frames[0];
    const auto progress = plan.consume(reply);
    REQUIRE(progress.kind == ProgressKind::complete);
    REQUIRE(progress.origin == protocol::Origin::command_feedback);
    const auto data = std::any_cast<o6::AngleData>(progress.value);
    REQUIRE(data.origin == o6::SampleOrigin::CommandFeedback);
    REQUIRE(data.device_raw[5] == 6);
}
TEST_CASE("O6 current query and current-limit write preserve opcode 0x02", "[protocol][o6]") {
    const std::array<uint8_t, 6> values{1, 2, 3, 4, 5, 6};
    const auto read = o6::protocol::read_current(0x28);
    const auto write = o6::protocol::write_current_limit(0x28, values);
    REQUIRE(read.domain == "current");
    REQUIRE(read.frames[0].dlc == 1);
    REQUIRE(read.frames[0].data[0] == 0x02);
    REQUIRE(write.domain == "current_limit");
    REQUIRE(write.frames[0].dlc == 7);
    REQUIRE(write.frames[0].data[0] == 0x02);
    REQUIRE(write.frames[0].data[6] == 6);
}
TEST_CASE("O6 protocol tactile assembly is transaction local and strict", "[protocol][o6]") {
    auto plan = o6::protocol::read_force_sensor_all(0x28);
    REQUIRE(plan.frames.size() == 5);
    REQUIRE(plan.keys.size() == 5);
    auto fresh = plan;
    Message row;
    row.id = 0x28;
    row.dlc = 6;
    row.data[0] = 0xB1;
    row.data[2] = 42;
    row.data[1] = 1;
    REQUIRE(plan.consume(row).kind == ProgressKind::unmatched);
    row.data[1] = 0;
    REQUIRE(plan.consume(row).kind == ProgressKind::pending);
    REQUIRE(plan.consume(row).duplicates == 1);
    SECTION("conflicting duplicate fails without completing the batch") {
        row.data[2] = 99;
        REQUIRE(plan.consume(row).kind == ProgressKind::failed);
    }
    SECTION("out of order complete fingers are published separately") {
        for (int finger = 4; finger >= 0; --finger) {
            for (int n = 9; n >= 0; --n) {
                row.data[0] = static_cast<uint8_t>(0xB1 + finger);
                row.data[1] = static_cast<uint8_t>(n << 4);
                auto result = plan.consume(row);
                if (finger == 0 && n == 0) {
                    // row zero was already present: completion occurs at row one.
                    REQUIRE(result.value.has_value());
                }
            }
        }
        row.data[0] = 0xB5;
        row.data[1] = 0x90;
        REQUIRE(fresh.consume(row).kind == ProgressKind::pending);
    }
}
TEST_CASE("O6 protocol accepts tactile rows with a reserved trailing byte", "[protocol][o6]") {
    auto plan = o6::protocol::read_force_sensor_all(0x28);
    Message row;
    row.id = 0x28;
    row.dlc = 7;
    row.data[6] = 0x5a;
    for (uint8_t finger = 0; finger < 5; ++finger) {
        for (uint8_t n = 0; n < 10; ++n) {
            row.data[0] = static_cast<uint8_t>(0xB1 + finger);
            row.data[1] = static_cast<uint8_t>(n << 4);
            const auto progress = plan.consume(row);
            if (finger == 4 && n == 9) {
                REQUIRE(progress.kind == ProgressKind::complete);
            } else {
                REQUIRE(progress.kind == ProgressKind::pending);
            }
        }
    }
}
TEST_CASE("O6 protocol compacts ten sparse tactile row markers", "[protocol][o6]") {
    auto plan = o6::protocol::read_force_sensor(0x28, 0xB4, "force_sensor_ring");
    Message row;
    row.id = 0x28;
    row.dlc = 7;
    row.data[0] = 0xB4;
    const std::array<uint8_t, 10> markers{0, 1, 2, 3, 4, 5, 7, 8, 9, 10};
    for (size_t i = 0; i < markers.size(); ++i) {
        row.data[1] = static_cast<uint8_t>(markers[i] << 4);
        row.data[2] = markers[i];
        const auto progress = plan.consume(row);
        if (i + 1 == markers.size()) {
            REQUIRE(progress.kind == ProgressKind::complete);
            const auto data = std::any_cast<o6::ForceSensorData>(progress.value);
            REQUIRE(data.values[6][0] == 7);
            REQUIRE(data.values[9][0] == 10);
        } else {
            REQUIRE(progress.kind == ProgressKind::pending);
        }
    }
}
TEST_CASE("O6 protocol serial fragments reject short and conflicting data", "[protocol][o6]") {
    auto plan = o6::protocol::read_version_sn(0x28);
    Message frame;
    frame.id = 0x28;
    frame.data[0] = 0xC0;
    frame.dlc = 7;
    REQUIRE(plan.consume(frame).kind == ProgressKind::unmatched);
    frame.dlc = 8;
    frame.data[2] = 'A';
    REQUIRE(plan.consume(frame).kind == ProgressKind::pending);
    REQUIRE(plan.consume(frame).duplicates == 1);
    SECTION("conflict") {
        frame.data[2] = 'B';
        REQUIRE(plan.consume(frame).kind == ProgressKind::failed);
    }
    SECTION("reordered completion") {
        for (int n : {3, 2, 1}) {
            frame.data[1] = static_cast<uint8_t>(n);
            auto result = plan.consume(frame);
            REQUIRE(result.kind == (n == 1 ? ProgressKind::complete : ProgressKind::pending));
        }
    }
}
