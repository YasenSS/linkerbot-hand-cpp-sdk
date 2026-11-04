#include "hand/support/optional_value.hpp"
#include "hand_device_transport_probe.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

namespace {
    std::string marker(const std::string &path) {
        std::ifstream file(std::filesystem::u8path(path));
        std::string value;
        file >> value;
        return value;
    }
    std::chrono::milliseconds operator_wait_timeout() {
        const auto value = env("HAND_DEVICE_DISCONNECT_WAIT_MS");
        const auto parsed = value ? parse_u32(value) : std::optional<uint32_t>{300000};
        REQUIRE(parsed.has_value());
        REQUIRE(test_support::require_value(parsed) >= 1000);
        REQUIRE(test_support::require_value(parsed) <= 1800000);
        return std::chrono::milliseconds{test_support::require_value(parsed)};
    }
    void wait_marker(const std::string &path, const std::string &expected, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (marker(path) != expected && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(100ms);
        }
        INFO("Waiting for operator marker " << path << " = " << expected);
        REQUIRE(marker(path) == expected);
    }
}

TEST_CASE("HandDevice hardware physical disconnect and reconnect", "[hardware][device][disconnect][manual]") {
    if (env("HAND_DEVICE_ENABLE_DISCONNECT_TESTS") != "1") {
        SKIP("set HAND_DEVICE_ENABLE_DISCONNECT_TESTS=1 for operator-assisted physical disconnect");
    }
    const auto signal = env("HAND_DEVICE_DISCONNECT_SIGNAL");
    if (!signal) {
        SKIP("set HAND_DEVICE_DISCONNECT_SIGNAL to an operator marker file outside the repository");
    }
    const auto wait = operator_wait_timeout();
    REQUIRE(marker(test_support::require_value(signal)) != "disconnected");
    REQUIRE(marker(test_support::require_value(signal)) != "reconnected");
    ProbedFixture fixture;
    auto &hand = *fixture.hand;
    hand.open();
    const auto identity = test_support::require_value(hand.get_identity());
    hand.start_polling({{TelemetrySemantic::kAngles, 100ms}});
    const auto before = hand.get_snapshot();
    std::cout << "Disconnect the hand link/power, then write disconnected to " << test_support::require_value(signal)
              << " (" << wait.count() / 1000 << " second deadline). No motion commands are issued.\n"
              << std::flush;
    wait_marker(test_support::require_value(signal), "disconnected", wait);
    REQUIRE_THROWS_AS(hand.get_angles(500ms), HandError);
    std::cout << "Reconnect the same hand and restore the CAN interface, then write reconnected to "
              << test_support::require_value(signal) << " (" << wait.count() / 1000 << " second deadline).\n"
              << std::flush;
    wait_marker(test_support::require_value(signal), "reconnected", wait);
    hand.recover();
    REQUIRE(hand.is_open());
    REQUIRE(hand.get_health().connected());
    require_identity_equal(test_support::require_value(hand.get_identity()), identity);
    REQUIRE(hand.get_health().polling_active);
    const auto after = hand.get_snapshot();
    REQUIRE(after.connection_generation > before.connection_generation);
    REQUIRE(after.angles.has_value());
    require_meta(test_support::require_value(after.angles).meta, after.connection_generation);
    REQUIRE(test_support::require_value(test_support::require_value(after.angles).meta.collected_at) >
            test_support::require_value(test_support::require_value(before.angles).meta.collected_at));
    const auto next = hand.wait_for_snapshot(after, 2s);
    REQUIRE(next.snapshot_sequence > after.snapshot_sequence);
    hand.stop_polling();
}
