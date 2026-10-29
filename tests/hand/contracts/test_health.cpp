#include <catch2/catch_test_macros.hpp>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/health.hpp>
#include <linkerbot/hand/l20/l20.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l6/l6.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o6/o6.hpp>
#include <linkerbot/transport/session.hpp>

#include <algorithm>
#include <chrono>
#include <type_traits>
#include <utility>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

TEST_CASE("hand health has an explicit safe zero state", "[health]") {
    const HandHealth health;

    REQUIRE(health.lifecycle_state == LifecycleState::kClosed);
    REQUIRE(health.connection_state == ConnectionState::kClosed);
    REQUIRE_FALSE(health.connected());
    REQUIRE_FALSE(health.identity_verified);
    REQUIRE_FALSE(health.polling_active);
    REQUIRE(health.connection_generation == 0);
    REQUIRE(health.reconnect_count == 0);
    REQUIRE(health.consecutive_errors == 0);
    REQUIRE_FALSE(health.recent_error.has_value());
    REQUIRE(health.channels.empty());
    REQUIRE(health.command_families.empty());
    REQUIRE(health.pending_commands() == 0);
    REQUIRE(health.superseded_commands() == 0);
    REQUIRE(health.transport.state == linkerbot::can::TransportState::kClosed);
    REQUIRE_FALSE(health.session.open);
}

TEST_CASE("channel health owns rate latency queue and generation data", "[health]") {
    ChannelMetrics channel;
    channel.channel = "angles";
    channel.requested_hz = 100.0;
    channel.achieved_hz = 96.5;
    channel.missed_deadlines = 3;
    channel.timeouts = 2;
    channel.completed_reads = 193;
    channel.feedback_samples = 194;
    channel.data_age = {true, false, 2ms, 194, 7};
    channel.latency = {128, 1ms, 3ms, 8ms};
    channel.queue = {2, 9, 220, 4};

    REQUIRE(channel.channel == "angles");
    REQUIRE(channel.requested_hz == 100.0);
    REQUIRE(channel.achieved_hz == 96.5);
    REQUIRE(channel.missed_deadlines == 3);
    REQUIRE(channel.timeouts == 2);
    REQUIRE(channel.data_age.valid);
    REQUIRE_FALSE(channel.data_age.stale);
    REQUIRE(channel.data_age.age == 2ms);
    REQUIRE(channel.data_age.sequence == 194);
    REQUIRE(channel.data_age.generation == 7);
    REQUIRE(channel.latency.count == 128);
    REQUIRE(channel.latency.p95 == 3ms);
    REQUIRE(channel.queue.depth == 2);
    REQUIRE(channel.queue.high_water == 9);
    REQUIRE(channel.queue.accepted == 220);
    REQUIRE(channel.queue.dropped == 4);
}

TEST_CASE("hand health carries structured errors and command family totals", "[health]") {
    HandHealth health;
    health.lifecycle_state = LifecycleState::kOpen;
    health.connection_state = ConnectionState::kConnected;
    health.identity_verified = true;
    health.connection_generation = 11;

    ErrorInfo error;
    error.code = ErrorCode::kTimeout;
    error.message = "angles response timed out";
    error.error_type = "TimeoutError";
    error.context.operation = "get_angles";
    error.context.model = "O6";
    error.context.channel = "angles";
    error.context.generation = 11;
    health.recent_error = std::move(error);

    CommandFamilyMetrics angles;
    angles.family = "angle_target";
    angles.pending = 1;
    angles.accepted = 20;
    angles.dispatched = 18;
    angles.acknowledged = 17;
    angles.failed = 1;
    angles.superseded = 4;
    angles.queue = {1, 3, 20, 4};

    CommandFamilyMetrics discrete;
    discrete.family = "discrete";
    discrete.pending = 2;
    discrete.superseded = 0;
    health.command_families = {angles, discrete};

    REQUIRE(health.connected());
    REQUIRE(health.recent_error->code == ErrorCode::kTimeout);
    REQUIRE(health.recent_error->context.operation == "get_angles");
    REQUIRE(health.recent_error->context.model == "O6");
    REQUIRE(health.recent_error->context.channel == "angles");
    REQUIRE(health.recent_error->context.generation == 11);
    REQUIRE(health.pending_commands() == 3);
    REQUIRE(health.superseded_commands() == 4);
}

TEST_CASE("hand health embeds transport and session snapshots", "[health]") {
    HandHealth health;
    health.transport.state = linkerbot::can::TransportState::kOpen;
    health.transport.generation = 5;
    health.transport.pending_tx = 4;
    health.session.open = true;
    health.session.generation = 5;
    health.session.pending_tx = 3;
    health.session.queue_high_water = 12;
    health.session.overflows = 2;

    REQUIRE(health.transport.connected());
    REQUIRE(health.transport.generation == 5);
    REQUIRE(health.transport.pending_tx == 4);
    REQUIRE(health.session.open);
    REQUIRE(health.session.generation == 5);
    REQUIRE(health.session.pending_tx == 3);
    REQUIRE(health.session.queue_high_water == 12);
    REQUIRE(health.session.overflows == 2);
}

TEST_CASE("model facades declare canonical health channels", "[health][models]") {
    const auto verify = [](const HandHealth &health, bool speed_supported, bool stall_supported) {
        const auto find = [&](const char *name) {
            return std::find_if(health.channels.begin(), health.channels.end(),
                                [&](const auto &channel) { return channel.channel == name; });
        };
        for (const auto *name :
             {"angles", "speeds", "currents", "force", "temperatures", "faults", "version", "stall"}) {
            REQUIRE(find(name) != health.channels.end());
        }
        REQUIRE(find("speeds")->supported == speed_supported);
        REQUIRE(find("stall")->supported == stall_supported);
        REQUIRE_FALSE(find("stall")->data_age.valid);
        REQUIRE(find("stall")->requested_hz == 0.0);
    };
    const auto session = [] {
        return linkerbot::transport::Session::own(std::make_unique<linkerbot::can::FakeTransport>());
    };

    SECTION("L6") {
        auto bus = session();
        {
            l6::L6 hand(l6::Side::kLeft, bus);
            verify(hand.health(), false, true);
        }
        bus->close();
    }
    SECTION("O6") {
        auto bus = session();
        {
            o6::O6 hand(o6::Side::kLeft, bus);
            verify(hand.health(), true, true);
        }
        bus->close();
    }
    SECTION("L20") {
        auto bus = session();
        {
            l20::L20 hand(l20::Side::kLeft, bus);
            verify(hand.health(), true, false);
        }
        bus->close();
    }
    SECTION("L20lite") {
        auto bus = session();
        {
            l20lite::L20lite hand(l20lite::Side::kLeft, bus);
            verify(hand.health(), true, true);
        }
        bus->close();
    }
    SECTION("L30") {
        auto bus = session();
        {
            l30::L30 hand(bus, l30::kDefaultNodeId, l30::kDefaultHostId, false);
            verify(hand.health(), true, false);
        }
        bus->close();
    }
    SECTION("O30") {
        auto bus = session();
        {
            o30::O30 hand(bus);
            verify(hand.health(), true, true);
        }
        bus->close();
    }
}

static_assert(std::is_copy_constructible_v<HandHealth>);
static_assert(std::is_move_constructible_v<HandHealth>);
static_assert(std::is_same_v<decltype(std::declval<const l6::L6 &>().health()), HandHealth>);
static_assert(std::is_same_v<decltype(std::declval<const o6::O6 &>().health()), HandHealth>);
static_assert(std::is_same_v<decltype(std::declval<const l20::L20 &>().health()), HandHealth>);
static_assert(std::is_same_v<decltype(std::declval<const l20lite::L20lite &>().health()), HandHealth>);
static_assert(std::is_same_v<decltype(std::declval<const l30::L30 &>().health()), HandHealth>);
static_assert(std::is_same_v<decltype(std::declval<const o30::O30 &>().health()), HandHealth>);
