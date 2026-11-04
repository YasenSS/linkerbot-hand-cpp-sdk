#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <linkerbot/relay.hpp>
#include <thread>

using namespace std::chrono_literals;
using linkerbot::internal::DataRelay;
using linkerbot::internal::DataRelayTimeout;

TEST_CASE("comm: relay admission consumes deadline without sending", "[comm][relay][regression]") {
    DataRelay<int> relay;
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto first = std::async(std::launch::async, [&] {
        try {
            return relay.request(
                [&] {
                    entered.set_value();
                    gate.wait();
                },
                2.F);
        } catch (...) {
            return -1;
        }
    });
    entered.get_future().wait();
    std::atomic<bool> sent{false};
    auto second = std::async(std::launch::async, [&] {
        try {
            relay.request([&] { sent = true; }, 0.02F);
            return false;
        } catch (const DataRelayTimeout &) {
            return true;
        }
    });
    const auto status = second.wait_for(250ms);
    relay.cancel(std::make_exception_ptr(std::runtime_error("stop")));
    release.set_value();
    first.get();
    REQUIRE(status == std::future_status::ready);
    REQUIRE(second.get());
    REQUIRE_FALSE(sent);
    REQUIRE(relay.pending_count() == 0);
}

TEST_CASE("comm: relay cancellation wakes queued calls before active send returns", "[comm][relay][regression]") {
    DataRelay<int> relay;
    std::promise<void> entered, release, queued;
    auto gate = release.get_future().share();
    auto first = std::async(std::launch::async, [&] {
        try {
            relay.request(
                [&] {
                    entered.set_value();
                    gate.wait();
                },
                2.F);
        } catch (...) {
            return std::current_exception();
        }
        return std::exception_ptr{};
    });
    entered.get_future().wait();
    std::atomic<bool> sent{false};
    auto second = std::async(std::launch::async, [&] {
        queued.set_value();
        try {
            relay.request([&] { sent = true; }, 2.F);
            return false;
        } catch (const std::runtime_error &) {
            return true;
        }
    });
    queued.get_future().wait();
    relay.cancel(std::make_exception_ptr(std::runtime_error("stop")));
    const auto status = second.wait_for(250ms);
    release.set_value();
    const auto first_error = first.get();
    REQUIRE(first_error != nullptr);
    REQUIRE_THROWS_AS(std::rethrow_exception(first_error), std::runtime_error);
    REQUIRE(status == std::future_status::ready);
    REQUIRE(second.get());
    REQUIRE_FALSE(sent);
    REQUIRE(relay.pending_count() == 0);
    relay.resume();
    REQUIRE(relay.request([&] { relay.push(42); }, 0.1F) == 42);
}

TEST_CASE("comm: relay rejects response delivered after deadline while send is blocked", "[comm][relay][regression]") {
    DataRelay<int> relay;
    bool accepted = true;
    REQUIRE_THROWS_AS(relay.request_until(
                          [&] {
                              std::this_thread::sleep_for(30ms);
                              accepted = relay.push_if_waiting(42);
                          },
                          std::chrono::steady_clock::now() + 10ms),
                      DataRelayTimeout);
    REQUIRE_FALSE(accepted);
    REQUIRE_FALSE(relay.snapshot());
    REQUIRE(relay.pending_count() == 0);
}

TEST_CASE("comm: relay sink clear waits for entered callback", "[comm][relay][regression]") {
    DataRelay<int> relay;
    std::promise<void> entered, release, clearing;
    auto gate = release.get_future().share();
    relay.set_sink([&](int) {
        entered.set_value();
        gate.wait();
    });
    auto delivery = std::async(std::launch::async, [&] { relay.push(1); });
    entered.get_future().wait();
    auto clear = std::async(std::launch::async, [&] {
        clearing.set_value();
        relay.set_sink({});
    });
    clearing.get_future().wait();
    const auto blocked = clear.wait_for(20ms);
    release.set_value();
    delivery.get();
    clear.get();
    REQUIRE(blocked == std::future_status::timeout);
    REQUIRE_NOTHROW(relay.push(2));
}

TEST_CASE("comm: relay self-cleared sink retains external cancellation barrier", "[comm][relay][regression]") {
    DataRelay<int> relay;
    std::promise<void> cancelled, release;
    auto gate = release.get_future().share();
    relay.set_sink([&](int) {
        relay.set_sink({});
        cancelled.set_value();
        gate.wait();
    });
    auto delivery = std::async(std::launch::async, [&] { relay.push(1); });
    cancelled.get_future().wait();
    auto clear = std::async(std::launch::async, [&] { relay.set_sink({}); });
    const auto blocked = clear.wait_for(20ms);
    release.set_value();
    delivery.get();
    clear.get();
    REQUIRE(blocked == std::future_status::timeout);
}

TEST_CASE("comm: relay throwing sink releases cancellation barrier", "[comm][relay][regression]") {
    DataRelay<int> relay;
    relay.set_sink([](int) { throw std::runtime_error("callback failure"); });
    REQUIRE_THROWS_AS(relay.push(1), std::runtime_error);
    REQUIRE_NOTHROW(relay.set_sink({}));
    REQUIRE(relay.request([&] { relay.push(2); }, 0.1F) == 2);
}

#include <limits>
TEST_CASE("comm: relay rejects invalid timeout before registering or sending", "[comm][relay][regression]") {
    DataRelay<int> relay;
    bool sent = false;
    for (const float timeout : {0.F, -1.F, std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::max()}) {
        REQUIRE_THROWS_AS(relay.request([&] { sent = true; }, timeout), std::invalid_argument);
    }
    REQUIRE_FALSE(sent);
    REQUIRE(relay.pending_count() == 0);
}
