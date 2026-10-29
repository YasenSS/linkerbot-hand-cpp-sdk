#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "../../src/hand/identity/probe.hpp"

using namespace linkerbot::hand;

TEST_CASE("identity probe registry covers every supported hand model", "[identity][probe][contract]") {
    const auto &rules = identity_probe::model_rules();
    REQUIRE(rules.size() == 6);

    constexpr std::array<Model, 6> expected{Model::kL6,  Model::kO6,  Model::kL20Lite,
                                            Model::kL20, Model::kL30, Model::kO30};
    for (const auto model : expected) {
        REQUIRE(identity_probe::rule_for(model).model == model);
    }

    REQUIRE_THROWS_AS(identity_probe::rule_for(Model::kUnknown), UnsupportedOperationError);
}

TEST_CASE("identity probe registry recognizes Python classic factory prefixes", "[identity][probe][contract]") {
    REQUIRE(identity_probe::classic_model_from_product_code("LHL6-1-1-L") == Model::kL6);
    REQUIRE(identity_probe::classic_model_from_product_code("lho6-1-1-r") == Model::kO6);
    REQUIRE(identity_probe::classic_model_from_product_code("LHT10-1-1-L") == Model::kL20Lite);
    for (const auto prefix : {"L20", "LHT20", "L25", "LHL25", "LHG20"}) {
        REQUIRE(identity_probe::classic_model_from_product_code(std::string(prefix) + "-1-1-L") == Model::kL20);
    }
    REQUIRE(identity_probe::classic_model_from_product_code("UNKNOWN-1-1-L") == Model::kUnknown);
}

TEST_CASE("classic discovery product probe is identity-only", "[identity][probe][contract]") {
    const auto plan = identity_probe::classic_product_plan(0x28U);
    REQUIRE(plan.frames.size() == 1);
    REQUIRE(plan.frames.front().id == 0x28U);
    REQUIRE(plan.frames.front().dlc == 1);
    REQUIRE(plan.frames.front().data[0] == 0xC0);
    REQUIRE_FALSE(plan.frames.front().is_extended);
    REQUIRE_FALSE(plan.frames.front().is_fd);
    REQUIRE(plan.command_family.empty());
}
