#include "hand/o30/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"
#include <catch2/catch_test_macros.hpp>
#include <future>
#include <linkerbot/hand/o6/o6.hpp>
using namespace std::chrono_literals;
TEST_CASE("comm: O6 timeout and close leave shared O30 usable", "[comm][o6][o30][runtime-migration]") {
    using namespace linkerbot::hand;
    auto fake = std::make_unique<linkerbot::can::FakeTransport>();
    auto *raw = fake.get();
    auto hop = std::make_shared<o30_comm::HopEndpoint>();
    hop->seed_defaults();
    o30_comm::attach_hop(raw, hop);
    auto session = linkerbot::transport::Session::own(std::move(fake));
    o6::O6 hand6(o6::Side::kLeft, session, "fake", test_util::unique_mapping_path());
    o30::O30 hand30(session);
    REQUIRE_THROWS(hand6.speed.get_blocking(10ms));
    hand6.stop();
    REQUIRE_NOTHROW(hand30.speed.get_blocking(500ms));
    REQUIRE(hand30.runtime_health().state == runtime::DeviceState::ready);
    REQUIRE(hand30.runtime_health().in_flight == 0);
}
