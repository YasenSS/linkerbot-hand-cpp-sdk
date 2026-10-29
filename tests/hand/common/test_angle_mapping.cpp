#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <linkerbot/hand/angle_mapping.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::hand::AngleMapping;
using linkerbot::hand::AngleMappingManager;
using linkerbot::hand::AngleMappingRow;
using linkerbot::hand::default_angle_mapping;

namespace {

    AngleMapping reverse_mapping(std::size_t joint_count) {
        auto mapping = default_angle_mapping(joint_count);
        for (auto &row : mapping) {
            std::reverse(row.begin(), row.end());
        }
        return mapping;
    }

} // namespace

TEST_CASE("comm: default angle mapping is linear", "[comm][angle_mapping]") {
    const auto mapping = default_angle_mapping(6);
    REQUIRE(mapping.size() == 6);
    REQUIRE(mapping[0][0] == 0);
    REQUIRE(mapping[0][255] == 255);
    REQUIRE(mapping[5][128] == 128);
}

TEST_CASE("comm: mapping manager creates default toml", "[comm][angle_mapping]") {
    const auto path = test_util::unique_mapping_path();
    AngleMappingManager manager("l6", {"a", "b"}, path);

    REQUIRE(std::filesystem::exists(path));
    REQUIRE(manager.map_values({0, 255}) == std::vector<std::uint8_t>{0, 255});
    REQUIRE(manager.get_mapping() == default_angle_mapping(2));
}

TEST_CASE("comm: mapping manager preserves other model sections", "[comm][angle_mapping]") {
    const auto path = test_util::unique_mapping_path();
    AngleMappingManager l6("l6", {"a", "b"}, path);
    const auto custom = reverse_mapping(2);
    l6.set_mapping(custom);

    AngleMappingManager l20("l20lite", {"c", "d", "e"}, path);

    REQUIRE(l20.map_values({1, 2, 3}) == std::vector<std::uint8_t>{1, 2, 3});
    REQUIRE(AngleMappingManager("l6", {"a", "b"}, path).get_mapping() == custom);
}

TEST_CASE("comm: mapping manager distinguishes side and interface", "[comm][angle_mapping]") {
    const auto path = test_util::unique_mapping_path();
    AngleMappingManager left_can0("l6", {"a", "b"}, path, "left", "can0");
    left_can0.set_mapping(reverse_mapping(2));

    AngleMappingManager right_can0("l6", {"a", "b"}, path, "right", "can0");
    AngleMappingManager left_can1("l6", {"a", "b"}, path, "left", "can1");

    REQUIRE(left_can0.map_values({0, 128}) == std::vector<std::uint8_t>{255, 127});
    REQUIRE(right_can0.map_values({0, 128}) == std::vector<std::uint8_t>{0, 128});
    REQUIRE(left_can1.map_values({0, 128}) == std::vector<std::uint8_t>{0, 128});
}

TEST_CASE("comm: mapping manager reloads saved mapping", "[comm][angle_mapping]") {
    const auto path = test_util::unique_mapping_path();
    AngleMappingManager manager("l6", {"a", "b"}, path);
    const auto custom = reverse_mapping(2);
    manager.set_mapping(custom);

    AngleMappingManager reloaded("l6", {"a", "b"}, path);
    REQUIRE(reloaded.map_values({0, 128}) == std::vector<std::uint8_t>{255, 127});
}

TEST_CASE("comm: mapping manager rejects mismatched joint names", "[comm][angle_mapping]") {
    const auto path = test_util::unique_mapping_path();
    REQUIRE_NOTHROW(AngleMappingManager("l6", {"a", "b"}, path));

    REQUIRE_THROWS_AS(AngleMappingManager("l6", {"b", "a"}, path), linkerbot::hand::ValidationError);
}

TEST_CASE("comm: mapping manager rejects invalid mapping shape", "[comm][angle_mapping]") {
    AngleMappingManager manager("l6", {"a", "b"}, test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(manager.set_mapping({AngleMappingRow{}}), linkerbot::hand::ValidationError);

    auto short_row = default_angle_mapping(2);
    short_row.pop_back();
    REQUIRE_THROWS_AS(manager.set_mapping(short_row), linkerbot::hand::ValidationError);
}

TEST_CASE("comm: mapping manager rejects invalid raw values", "[comm][angle_mapping]") {
    AngleMappingManager manager("l6", {"a", "b"}, test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(manager.map_values({0}), linkerbot::hand::ValidationError);
}
