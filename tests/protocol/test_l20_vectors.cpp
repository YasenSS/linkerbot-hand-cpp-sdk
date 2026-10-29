#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/l20/protocol_backend.hpp>
using namespace linkerbot::hand;
using linkerbot::can::Message;
using protocol::ProgressKind;
TEST_CASE("L20 five frame domains reject malformed and conflicting data and retain reserved slots", "[protocol][l20]") {
    l20::protocol::Backend backend(0x28);
    for (auto name : {"angle", "speed", "current", "temperature", "fault"}) {
        INFO(name);
        auto plan = backend.make_read(name), fresh = plan;
        REQUIRE(plan.frames.size() == 5);
        REQUIRE(plan.gap == std::chrono::microseconds(2500));
        for (int i = 4; i >= 0; --i) {
            auto m = plan.frames[i];
            m.dlc = 7;
            for (int byte = 1; byte < 7; ++byte) {
                m.data[byte] = i * 10 + byte;
            }
            for (int mode = 0; mode < 6; ++mode) {
                auto bad = m;
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
            auto result = plan.consume(m);
            REQUIRE(result.kind == (i == 0 ? ProgressKind::complete : ProgressKind::pending));
            if (i == 4) {
                REQUIRE(plan.consume(m).duplicates == 1);
            }
            if (i == 0) {
                REQUIRE(fresh.consume(m).kind == ProgressKind::pending);
                // Even reserved bytes participate in duplicate conflict checks.
                m.data[4] = 99;
                REQUIRE(fresh.consume(m).kind == ProgressKind::failed);
            }
        }
    }
    std::array<uint8_t, l20::kJointCount> values{};
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = i + 1;
    }
    auto write = backend.make_command("angle", values);
    REQUIRE(write.frames[0].data[2] == 2);
    REQUIRE(write.frames[0].data[6] == 4);
    REQUIRE(write.frames[4].data[6] == 16);
    for (const auto &m : write.frames) {
        REQUIRE(m.data[4] == 0);
        REQUIRE(m.data[5] == 0);
        if (m.data[0] != 0x41) {
            REQUIRE(m.data[2] == 0);
        }
    }
    REQUIRE(write.gap == std::chrono::microseconds(500));
    REQUIRE(write.supersede_optional_feedback);
    REQUIRE(write.optional_response_window == std::chrono::milliseconds(200));
}
TEST_CASE("L20 tactile batch is exclusive and retains only complete aggregate samples", "[protocol][l20]") {
    auto batch = l20::protocol::read_force_sensor_all(0x28);
    auto single = l20::protocol::read_force_sensor(0x28, 0xB1, "force_sensor_thumb");
    REQUIRE(batch.exclusion_groups == single.exclusion_groups);
    REQUIRE(batch.preserve_last_success);
    REQUIRE(batch.publish_after_send);
    Message m;
    m.id = 0x28;
    m.dlc = 8;
    m.data[0] = 0xB1;
    m.data[1] = 1;
    REQUIRE(batch.consume(m).kind == ProgressKind::unmatched);
    m.data[1] = 0;
    REQUIRE(single.consume(m).kind == ProgressKind::pending);
    REQUIRE(single.consume(m).duplicates == 1);
    ++m.data[2];
    REQUIRE(single.consume(m).kind == ProgressKind::failed);
    for (int finger = 4; finger >= 0; --finger) {
        for (int row = 11; row >= 0; --row) {
            m.data[0] = 0xB1 + finger;
            m.data[1] = row << 4;
            auto result = batch.consume(m);
            REQUIRE(result.kind == (finger == 0 && row == 0 ? ProgressKind::complete : ProgressKind::pending));
            REQUIRE(result.updates.size() == (row == 0 ? 1 : 0));
        }
    }
}
TEST_CASE("L20 serial requires all four exact numbered slots", "[protocol][l20]") {
    auto plan = l20::protocol::read_version_sn(0x28);
    Message m;
    m.id = 0x28;
    m.dlc = 8;
    m.data[0] = 0xC0;
    m.data[1] = 6;
    REQUIRE(plan.consume(m).kind == ProgressKind::unmatched);
    for (int slot : {3, 1, 0, 2}) {
        m.data[1] = slot;
        m.dlc = 7;
        REQUIRE(plan.consume(m).kind == (slot == 3 ? ProgressKind::pending : ProgressKind::unmatched));
        m.dlc = 8;
        REQUIRE(plan.consume(m).kind == (slot == 2 ? ProgressKind::complete : ProgressKind::pending));
    }
}
TEST_CASE("L20 current and current-limit plans retain 0x51 through 0x55", "[protocol][l20]") {
    std::array<uint8_t, l20::kJointCount> values{};
    values.fill(77);
    const auto read = l20::protocol::read_current(0x28);
    const auto write = l20::protocol::write_current_limit(0x28, values);
    REQUIRE(read.domain == "current");
    REQUIRE(write.domain == "current");
    REQUIRE(read.frames.size() == 5);
    REQUIRE(write.frames.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(read.frames[i].data[0] == l20::kCurrentFrames[i].cmd);
        REQUIRE(write.frames[i].data[0] == l20::kCurrentFrames[i].cmd);
    }
}
