#include "hand/support/temp_path.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20/l20.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>
#include <linkerbot/hand/l6/l6.hpp>
#include <linkerbot/hand/o6/o6.hpp>
#include <memory>
#include <stdexcept>
#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

using linkerbot::can::FakeTransport;
namespace l6 = linkerbot::hand::l6;
namespace o6 = linkerbot::hand::o6;
namespace l20 = linkerbot::hand::l20;
namespace l20lite = linkerbot::hand::l20lite;

namespace {
    // exec a fresh test process: the suite may already have an event executor,
    // so running arbitrary C++ in a post-fork child would inherit unsafe locks.
    bool isolated(const char *selector) {
#if defined(__unix__) || defined(__APPLE__)
        pid_t pid;
        char *args[] = {const_cast<char *>(LINKERBOT_TEST_BINARY), const_cast<char *>(selector), nullptr};
        if (::posix_spawn(&pid, LINKERBOT_TEST_BINARY, nullptr, nullptr, args, environ)) {
            throw std::runtime_error("posix_spawn failed");
        }
        int status;
        while (::waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) {
                throw std::runtime_error("waitpid failed");
            }
        }
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
        (void)selector;
        SKIP("isolated lifecycle process tests require POSIX process support");
        return false;
#endif
    }
    void bound_child() {
#if defined(__unix__) || defined(__APPLE__)
        ::alarm(10);
#endif
    }
    template <class Hand, class Side> void destructor_failure(Side side) {
        auto transport = std::make_unique<FakeTransport>();
        transport->fail_next_closes(1);
        Hand hand(side, std::move(transport), "can0", test_util::unique_mapping_path());
    }
}

TEST_CASE("comm: L6 explicit stop() propagates a transport close failure", "[comm][lifecycle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l6::L6 hand(l6::Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    raw->fail_next_closes(1);
    REQUIRE_THROWS(hand.stop());
    REQUIRE_NOTHROW(hand.stop());
}
TEST_CASE("comm: L6 destructor absorbs a transport close failure", "[comm][lifecycle]") {
    const auto passed = isolated("[.child-l6-close]");
    REQUIRE(passed);
}
TEST_CASE("comm: O6 destructor absorbs a transport close failure", "[comm][lifecycle]") {
    const auto passed = isolated("[.child-o6-close]");
    REQUIRE(passed);
}
TEST_CASE("comm: L20 destructor absorbs a transport close failure", "[comm][lifecycle]") {
    const auto passed = isolated("[.child-l20-close]");
    REQUIRE(passed);
}
TEST_CASE("comm: L20lite destructor absorbs a transport close failure", "[comm][lifecycle]") {
    const auto passed = isolated("[.child-l20lite-close]");
    REQUIRE(passed);
}
TEST_CASE("comm: an O6 polling failure does not terminate the process", "[comm][lifecycle]") {
    const auto passed = isolated("[.child-o6-poll]");
    REQUIRE(passed);
}
TEST_CASE("child L6 close failure", "[.child-l6-close]") {
    bound_child();
    destructor_failure<l6::L6>(l6::Side::kLeft);
}
TEST_CASE("child O6 close failure", "[.child-o6-close]") {
    bound_child();
    destructor_failure<o6::O6>(o6::Side::kLeft);
}
TEST_CASE("child L20 close failure", "[.child-l20-close]") {
    bound_child();
    destructor_failure<l20::L20>(l20::Side::kLeft);
}
TEST_CASE("child L20lite close failure", "[.child-l20lite-close]") {
    bound_child();
    destructor_failure<l20lite::L20lite>(l20lite::Side::kLeft);
}
TEST_CASE("child O6 polling failure", "[.child-o6-poll]") {
    bound_child();
    auto transport = std::make_unique<FakeTransport>();
    transport->fail_writes("simulated polling failure");
    o6::O6 hand(o6::Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.start_polling({o6::SensorSource::kAngle}, std::chrono::milliseconds{5});
    REQUIRE(hand.wait_for_polling_fault(std::chrono::seconds{5}));
    REQUIRE(hand.last_polling_error().has_value());
    hand.stop_polling();
}
TEST_CASE("comm: an L20 polling failure sets an observable fault", "[comm][lifecycle]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->fail_writes("simulated polling failure");
    l20::L20 hand(l20::Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.start_polling({l20::SensorSource::kAngle}, std::chrono::milliseconds{5});
    REQUIRE(hand.wait_for_polling_fault(std::chrono::seconds{5}));
    REQUIRE(hand.last_polling_error().has_value());
    hand.stop_polling();
}
TEST_CASE("comm: an L20lite polling failure sets an observable fault", "[comm][lifecycle]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->fail_writes("simulated polling failure");
    l20lite::L20lite hand(l20lite::Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.start_polling({l20lite::SensorSource::kAngle}, std::chrono::milliseconds{5});
    REQUIRE(hand.wait_for_polling_fault(std::chrono::seconds{5}));
    REQUIRE(hand.last_polling_error().has_value());
    hand.stop_polling();
}

#include <atomic>
#include <future>
#include <linkerbot/relay.hpp>
#include <linkerbot/transport/session.hpp>

TEST_CASE("comm: cross callback cleanup does not create mutual join waits", "[comm][lifecycle][rfix]") {
    const auto session_passed = isolated("[.session-cross-release]");
    REQUIRE(session_passed);
    const auto relay_passed = isolated("[.relay-cross-cancel]");
    REQUIRE(relay_passed);
}
TEST_CASE("session cross writer release child", "[.session-cross-release]") {
    bound_child();
    using namespace std::chrono_literals;
    using linkerbot::transport::Session;
    std::shared_ptr<Session> first, second;
    std::promise<void> a_enter, b_enter, release;
    auto gate = release.get_future().share();
    auto a = std::make_unique<FakeTransport>();
    auto *araw = a.get();
    auto b = std::make_unique<FakeTransport>();
    auto *braw = b.get();
    a->on_request(1, [&](const auto &) {
        a_enter.set_value();
        gate.wait();
        second.reset();
        return std::vector<linkerbot::can::Message>{};
    });
    b->on_request(1, [&](const auto &) {
        b_enter.set_value();
        gate.wait();
        first.reset();
        return std::vector<linkerbot::can::Message>{};
    });
    first = Session::own(std::move(a));
    second = Session::own(std::move(b));
    linkerbot::can::Message message;
    message.id = 1;
    message.dlc = 1;
    auto ra = first->submit({{message}});
    auto rb = second->submit({{message}});
    a_enter.get_future().wait();
    b_enter.get_future().wait();
    release.set_value();
    REQUIRE_NOTHROW(ra.wait_until(std::chrono::steady_clock::now() + 2s));
    REQUIRE_NOTHROW(rb.wait_until(std::chrono::steady_clock::now() + 2s));
    Session::drain_deferred_cleanup();
    (void)araw;
    (void)braw;
}
TEST_CASE("relay cross generation self cancellation child", "[.relay-cross-cancel]") {
    bound_child();
    using namespace std::chrono_literals;
    linkerbot::internal::DataRelay<int> relay;
    std::promise<void> first_enter, second_enter, release;
    auto gate = release.get_future().share();
    std::atomic<unsigned> first_calls{0};
    std::atomic<bool> new_visible{false};
    relay.set_sink([&](int) {
        if (first_calls.fetch_add(1)) {
            return;
        }
        first_enter.set_value();
        gate.wait();
        relay.set_sink({});
    });
    auto first = std::async(std::launch::async, [&] { relay.push(1); });
    first_enter.get_future().wait();
    auto replace = std::async(std::launch::async, [&] {
        relay.set_sink([&](int) {
            new_visible = true;
            second_enter.set_value();
            gate.wait();
            relay.set_sink({});
        });
    });
    auto second = std::async(std::launch::async, [&] {
        while (!new_visible.load()) {
            relay.push(2);
            std::this_thread::yield();
        }
    });
    second_enter.get_future().wait();
    release.set_value();
    first.get();
    second.get();
    replace.get();
    REQUIRE_NOTHROW(relay.set_sink({}));
}
