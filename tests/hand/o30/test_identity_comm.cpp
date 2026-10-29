#include "hand/o30/comm_helpers.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace linkerbot::hand;

// 接受 O30i 历史名称，拒绝错误型号、侧别矛盾及未提供侧别证据的强校验。
TEST_CASE("comm: O30 optional identity verification", "[comm][o30][identity]") {
    auto h = o30_comm::make_hand();
    auto model = std::string("o30i-1-123-L");
    auto side = std::string("left");
    bool mismatch = false;
    bool malformed = false;
    SECTION("alias accepted") {}
    SECTION("wrong model") {
        model = "O6";
        mismatch = true;
    }
    SECTION("contradictory side") {
        side = "right";
        mismatch = true;
    }
    SECTION("missing side") {
        model = "O30";
        side = "";
        malformed = true;
    }
    SECTION("unknown side") {
        side = "unknown";
        malformed = true;
    }
    SECTION("wrong expected side") {
        model = "O30";
        side = "right";
        mismatch = true;
    }
    h.hop->put_ascii(0x41, 0, 16, model);
    h.hop->put_ascii(0x41, 0xD9, 8, side);
    const auto read = [&]() {
        return h.hand->version.get_verified_product_info(o30_comm::kTimeout, IdentitySide::kLeft);
    };
    if (mismatch) {
        REQUIRE_THROWS_AS(read(), IdentityMismatchError);
    } else if (malformed) {
        REQUIRE_THROWS_AS(read(), IdentityProtocolError);
    } else {
        REQUIRE(read().model == model);
    }
}
