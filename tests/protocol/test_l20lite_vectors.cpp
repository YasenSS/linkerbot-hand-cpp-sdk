#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/l20lite/protocol_backend.hpp>
using namespace linkerbot::hand;
using linkerbot::can::Message;
using protocol::ProgressKind;

TEST_CASE("L20Lite dual frame plans retain pacing and reject malformed or conflicting fragments",
          "[protocol][l20lite]") {
    l20lite::protocol::Backend backend(0x28);
    for (auto domain : {"angle", "current", "speed", "acceleration", "temperature", "fault"}) {
        INFO(domain);
        auto plan = backend.make_read(domain);
        auto fresh = plan;
        REQUIRE(plan.frames.size() == 2);
        REQUIRE(plan.gap == std::chrono::microseconds(1000));
        REQUIRE(plan.initial_delay == plan.gap);
        auto first = plan.frames[0], second = plan.frames[1];
        first.dlc = std::string(domain) == "angle" ? 7 : 6;
        second.dlc = std::string(domain) == "angle" ? 5 : 6;
        for (int mode = 0; mode < 6; ++mode) {
            auto bad = first;
            if (mode == 0) {
                --bad.dlc;
            }
            if (mode == 1) {
                ++bad.dlc;
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
        REQUIRE(plan.consume(second).kind == ProgressKind::pending);
        REQUIRE(plan.consume(second).duplicates == 1);
        REQUIRE(plan.consume(first).kind == ProgressKind::complete);
        REQUIRE(fresh.consume(first).kind == ProgressKind::pending);
        first.data[1] = 99;
        REQUIRE(fresh.consume(first).kind == ProgressKind::failed);
    }
    auto angle = backend.make_read("angle");
    REQUIRE(angle.gap == std::chrono::microseconds(1000));
    REQUIRE(angle.initial_delay == angle.gap);
}
TEST_CASE("L20Lite serial slots are strict byte offsets with optional fourth slot", "[protocol][l20lite]") {
    auto plan = l20lite::protocol::read_version_sn(0x28);
    Message m;
    m.id = 0x28;
    m.dlc = 8;
    m.data[0] = 0xC0;
    m.data[2] = 'A';
    m.data[1] = 1;
    REQUIRE(plan.consume(m).kind == ProgressKind::unmatched);
    m.data[1] = 18;
    m.dlc = 7;
    REQUIRE(plan.consume(m).kind == ProgressKind::unmatched);
    m.dlc = 8;
    REQUIRE(plan.consume(m).kind == ProgressKind::pending);
    for (auto offset : {12, 0, 6}) {
        m.data[1] = offset;
        auto result = plan.consume(m);
        REQUIRE(result.kind == (offset == 6 ? ProgressKind::complete : ProgressKind::pending));
        if (offset == 6) {
            REQUIRE(std::any_cast<std::string>(result.value).size() == 19);
        }
    }
}
TEST_CASE("L20Lite tip force groups four channels without mixing transactions", "[protocol][l20lite]") {
    auto plan = l20lite::protocol::read_tip_force(0x28), fresh = plan;
    REQUIRE(plan.frames.size() == 4);
    Message m;
    m.id = 0x28;
    m.dlc = 6;
    for (int cmd : {0x23, 0x21, 0x20, 0x22}) {
        m.data[0] = cmd;
        m.data[1] = cmd;
        auto result = plan.consume(m);
        REQUIRE(result.kind == (cmd == 0x22 ? ProgressKind::complete : ProgressKind::pending));
        if (cmd == 0x22) {
            auto v = std::any_cast<l20lite::TipForceData>(result.value);
            REQUIRE(v.tip_force.thumb.normal == 0x20);
            REQUIRE(v.tip_force.thumb.proximity == 0x23);
        }
    }
    REQUIRE(fresh.consume(m).kind == ProgressKind::pending);
    REQUIRE(fresh.consume(m).duplicates == 1);
    ++m.data[1];
    REQUIRE(fresh.consume(m).kind == ProgressKind::failed);
}
TEST_CASE("L20Lite tactile batches publish only complete data and exclude single requests", "[protocol][l20lite]") {
    auto plan = l20lite::protocol::read_force_sensor_all(0x28);
    auto single = l20lite::protocol::read_force_sensor(0x28, 0xB1, "force_sensor_thumb");
    REQUIRE(plan.exclusion_groups == single.exclusion_groups);
    REQUIRE(plan.gap == std::chrono::microseconds(2500));
    REQUIRE(plan.publish_after_send);
    REQUIRE(plan.preserve_last_success);
    Message m;
    m.id = 0x28;
    m.dlc = 8;
    m.data[0] = 0xB1;
    m.data[1] = 1;
    REQUIRE(plan.consume(m).kind == ProgressKind::unmatched);
    for (int finger = 4; finger >= 0; --finger) {
        for (int row = 11; row >= 0; --row) {
            m.data[0] = 0xB1 + finger;
            m.data[1] = row << 4;
            auto result = plan.consume(m);
            REQUIRE(result.kind == (finger == 0 && row == 0 ? ProgressKind::complete : ProgressKind::pending));
            REQUIRE(result.updates.size() == (row == 0 ? 1 : 0));
        }
    }
}
TEST_CASE("L20Lite stall writes assemble actual bank responses and reject active reads", "[protocol][l20lite]") {
    std::array<uint8_t, l20lite::kJointCount> raw{};
    raw.fill(50);
    REQUIRE_THROWS_AS(l20lite::protocol::read_stall_bank(0x28, 0xC6, "stall_time", raw), UnsupportedOperationError);
    l20lite::protocol::Backend backend{0x28};
    REQUIRE_THROWS_AS(backend.make_read("stall_time"), UnsupportedOperationError);
    REQUIRE_THROWS_AS(backend.make_read("stall_threshold"), UnsupportedOperationError);
    REQUIRE_THROWS_AS(backend.make_read("stall_torque"), UnsupportedOperationError);
    auto plan = l20lite::protocol::write_stall_bank(0x28, 0xC6, "stall_time", raw);
    REQUIRE(plan.publish_after_send);
    REQUIRE(plan.preserve_last_success);
    auto old = plan.frames[0];
    old.data[2] = 10;
    auto wrong_route = old;
    wrong_route.id = 0x27;
    REQUIRE(plan.consume(wrong_route).kind == ProgressKind::unmatched);
    auto invalid_bank = old;
    invalid_bank.data[1] = 3;
    REQUIRE(plan.consume(invalid_bank).kind == ProgressKind::unmatched);
    REQUIRE(plan.consume(old).kind == ProgressKind::pending);
    REQUIRE(plan.consume(old).duplicates == 1);
    const auto completed = plan.consume(plan.frames[1]);
    REQUIRE(completed.kind == ProgressKind::complete);
    REQUIRE(completed.origin == protocol::Origin::command_feedback);
    REQUIRE(std::any_cast<l20lite::StallTimeData>(completed.value).time.to_array()[0] == 100.0f);
    auto conflicting = l20lite::protocol::write_stall_bank(0x28, 0xC6, "stall_time", raw);
    REQUIRE(conflicting.consume(conflicting.frames[0]).kind == ProgressKind::pending);
    REQUIRE(conflicting.consume(old).kind == ProgressKind::failed);
}
TEST_CASE("L20Lite ID responses match the requested value", "[protocol][l20lite]") {
    auto config = l20lite::protocol::read_config_set_id(0x28, 1, 0x30);
    Message reply;
    reply.id = 0x28;
    reply.dlc = 3;
    reply.data = {0xC3, 0x29, 0x29};
    REQUIRE(config.consume(reply).kind == ProgressKind::unmatched);
    reply.data[1] = reply.data[2] = 0x30;
    REQUIRE(config.consume(reply).kind == ProgressKind::complete);
}
TEST_CASE("L20Lite current and current-limit plans retain 0x02 and 0x03", "[protocol][l20lite]") {
    std::array<uint8_t, l20lite::kJointCount> values{};
    values.fill(88);
    const auto read = l20lite::protocol::read_current(0x28);
    const auto write = l20lite::protocol::write_current_limit(0x28, values);
    REQUIRE(read.domain == "current");
    REQUIRE(write.domain == "current_limit");
    REQUIRE(read.frames[0].data[0] == 0x02);
    REQUIRE(read.frames[1].data[0] == 0x03);
    REQUIRE(write.frames[0].data[0] == 0x02);
    REQUIRE(write.frames[1].data[0] == 0x03);
}
