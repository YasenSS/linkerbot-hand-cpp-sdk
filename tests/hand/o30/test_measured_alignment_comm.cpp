#include "hand/o30/comm_helpers.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <limits>
#include <linkerbot/hand/errors.hpp>

using namespace linkerbot::hand::o30;
using namespace std::chrono_literals;

namespace {
    // Literal measured SI order. Do not derive expectations from the SDK table.
    constexpr std::array<uint8_t, 20> slots{0, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    constexpr std::array<uint8_t, 20> sparse_ids{0,  1, 6,  11, 16, 21, 2, 7,  12, 17,
                                                 22, 8, 13, 18, 23, 4,  9, 14, 19, 24};
}

TEST_CASE("comm: O30i physical names and endpoints follow measured summary", "[comm][o30][alignment][direction]") {
    REQUIRE(joint_name(0) == "thumb_rotation");
    REQUIRE(joint_name(1) == "thumb_roll");
    REQUIRE(kThumbRotationSi == 0x00);
    REQUIRE(kThumbRollSi == 0x05);
    REQUIRE(kJointSemantics[0].protocol_name == "roll_0");
    REQUIRE(kJointSemantics[0].min_pose == "away_from_index");
    REQUIRE(kJointSemantics[0].max_pose == "toward_index");
    REQUIRE(kJointSemantics[1].protocol_name == "yaw_0");
    REQUIRE(kJointSemantics[1].min_pose == "parallel_to_palm");
    REQUIRE(kJointSemantics[1].max_pose == "perpendicular_to_palm");
    for (size_t i = 2; i < 6; ++i) {
        REQUIRE(kJointSemantics[i].min_pose == "toward_thumb");
        REQUIRE(kJointSemantics[i].max_pose == "away_from_thumb");
    }
    for (size_t i = 6; i < 20; ++i) {
        REQUIRE(kJointSemantics[i].min_pose == "extended");
        REQUIRE(kJointSemantics[i].max_pose == "flexed");
    }
}

TEST_CASE("comm: O30i percentage writes and reads isolate all twenty measured axes",
          "[comm][o30][alignment][direction]") {
    const auto joint = GENERATE(Catch::Generators::range(size_t{0}, size_t{20}));
    const auto percent = GENERATE(0.0F, 25.0F, 50.0F, 75.0F, 100.0F);
    const auto object_overload = GENERATE(false, true);
    const auto mapped = joint < 6 ? percent : 100.0F - percent;
    const uint8_t raw = mapped == 0 ? 0 : mapped == 25 ? 64 : mapped == 50 ? 128 : mapped == 75 ? 191 : 255;
    auto h = o30_comm::make_hand();
    h.hop->put(0x01, std::vector<uint8_t>(36, 128));
    auto target = o30_comm::filled_percent(50.0F);
    target[joint] = percent;
    if (object_overload) {
        h.hand->angle.set_angles_percent(Angle::from_array(target));
    } else {
        h.hand->angle.set_angles_percent(target);
    }
    auto expected = std::vector<uint8_t>(36, 128);
    expected[slots[joint]] = raw;
    REQUIRE(h.hop->objects.at({0x01, false}) == expected);
    REQUIRE(h.hop->writes.size() == 3);
    const auto read = h.hand->angle.get_blocking();
    REQUIRE(read.angles[joint] == Catch::Approx(float(joint < 6 ? raw : 255 - raw) * 100 / 255));
    REQUIRE(h.hand->angle.get_snapshot()->angles[joint] == read.angles[joint]);
}

TEST_CASE("comm: O30i sparse APIs map every mounted joint on both HOP revisions", "[comm][o30][alignment][sparse]") {
    const auto joint = GENERATE(Catch::Generators::range(size_t{0}, size_t{20}));
    const auto modern = GENERATE(false, true);
    const auto by_sdk_index = GENERATE(false, true);
    const auto raw = GENERATE(uint8_t{0}, uint8_t{255});
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(0x41, 0x61, 8, modern ? "0.0.4" : "0.0.3");
    h.hop->put(0x01, std::vector<uint8_t>(36, 0xA5));
    if (by_sdk_index) {
        h.hand->angle.set_sparse_joints({{joint, raw}});
    } else {
        h.hand->angle.set_sparse({{sparse_ids[joint], raw}});
    }
    REQUIRE(h.hop->writes.size() == 1);
    if (modern) {
        REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{1, slots[joint], 1, raw});
        auto expected = std::vector<uint8_t>(36, 0xA5);
        expected[slots[joint]] = raw;
        REQUIRE(h.hop->objects.at({0x01, false}) == expected);
    } else {
        REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x30, 0, 2, sparse_ids[joint], raw});
    }
}

TEST_CASE("comm: O30i sparse validation rejects unmounted IDs before any I/O", "[comm][o30][alignment][sparse]") {
    const auto id =
        GENERATE(uint8_t{3}, uint8_t{5}, uint8_t{10}, uint8_t{15}, uint8_t{20}, uint8_t{25}, uint8_t{35}, uint8_t{255});
    auto h = o30_comm::make_hand();
    REQUIRE_THROWS_AS(h.hand->angle.set_sparse({{9, 25}, {id, 50}}), linkerbot::hand::ValidationError);
    REQUIRE(h.transport->sent().empty());
}

TEST_CASE("comm: O30i helpers handle nonfinite and extreme percentages deterministically",
          "[comm][o30][alignment][direction]") {
    REQUIRE(percent_to_raw(std::numeric_limits<float>::max()) == 0);
    REQUIRE(percent_to_raw(std::numeric_limits<float>::lowest()) == 255);
    REQUIRE_THROWS_AS(percent_to_raw(std::numeric_limits<float>::quiet_NaN()), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(percent_to_raw(std::numeric_limits<float>::infinity()), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(joint_percent_to_raw(0, -std::numeric_limits<float>::infinity()),
                      linkerbot::hand::ValidationError);
}

TEST_CASE("comm: O30i all percentage group APIs reject invalid values before sending",
          "[comm][o30][alignment][angle]") {
    const auto value =
        GENERATE(-1.0F, 101.0F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    auto h = o30_comm::make_hand();
    REQUIRE_THROWS_AS(h.hand->angle.set_tips({0, 0, 0, 0, value}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->angle.set_roots1({0, 0, 0, 0, value}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->angle.set_roots2({0, 0, 0, value}), linkerbot::hand::ValidationError);
    REQUIRE(h.transport->sent().empty());
}

TEST_CASE("comm: O30i revision selects fault object without misreading valid masks", "[comm][o30][alignment][fault]") {
    const auto version = GENERATE("0.0.3", "0.0.4", "V0.0.3", "v0.0.4");
    const bool modern = std::string(version).back() == '4';
    const auto kind = GENERATE(ValueKind::kActual, ValueKind::kSetpoint);
    const uint8_t mi = modern ? 0x0C : 0x0D;
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(0x41, 0x61, 8, version);
    h.hop->put(modern ? 0x0D : 0x0C, std::vector<uint8_t>(36, 0x0B));
    std::vector<uint8_t> wire(36, 0);
    wire[21] = 4;
    h.hop->put(mi, wire, kind == ValueKind::kSetpoint);
    const auto data = h.hand->fault.get_blocking(500ms, kind);
    REQUIRE(data.kind == kind);
    REQUIRE(data.revision == (modern ? protocol::Revision::kV004 : protocol::Revision::kV003));
    REQUIRE(data.has_any_fault(16));
    REQUIRE(data.has_fault(16, FaultCode::kOverCurrent) == modern);
    REQUIRE(data.status(16).known_bits == (modern ? 4 : 0));
    REQUIRE(data.status(16).reserved_bits == (modern ? 0 : 4));
    for (size_t i = 0; i < 20; ++i) {
        REQUIRE(data.faults[i] == (i == 16 ? 4 : 0));
    }
    REQUIRE(o30_comm::last_payload(h.transport) ==
            std::vector<uint8_t>{static_cast<uint8_t>(mi | (kind == ValueKind::kSetpoint ? 0x80 : 0)), 0, 36});
    REQUIRE(h.hand->fault.get_snapshot()->faults == data.faults);
    REQUIRE(h.hand->get_snapshot().fault->faults == data.faults);
    h.transport->clear_sent();
    (void)h.hand->fault.get_blocking(500ms, kind);
    REQUIRE(h.transport->sent().size() == 1); // cached version
}

TEST_CASE("comm: O30i unknown revision refuses fault reads and current writes", "[comm][o30][alignment][fault]") {
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(0x41, 0x61, 8, "0.0.9");
    REQUIRE_THROWS_AS(h.hand->fault.get_blocking(), protocol::ProtocolError);
    REQUIRE_THROWS_AS(h.hand->current.set_all_current_limits(42), protocol::ProtocolError);
    REQUIRE(h.hop->writes.empty());
    for (const auto &msg : h.transport->sent()) {
        REQUIRE(o30_comm::hop_useful(msg) == std::vector<uint8_t>{0x41, 0x61, 8});
    }
}

TEST_CASE("comm: O30i legacy fault bytes do not acquire unverified bit meanings", "[comm][o30][alignment][fault]") {
    const auto value = GENERATE(uint8_t{0}, uint8_t{4}, uint8_t{0x30}, uint8_t{0xFF});
    auto h = o30_comm::make_hand();
    h.hop->put(0x0D, std::vector<uint8_t>(36, value));
    const auto data = h.hand->fault.get_blocking();
    REQUIRE(data.status(0).raw == value);
    REQUIRE(data.status(0).known_bits == 0);
    REQUIRE(data.status(0).reserved_bits == value);
    REQUIRE(data.has_any_fault(0) == (value != 0));
    REQUIRE_FALSE(data.has_fault(0, FaultCode::kOverCurrent));
}
