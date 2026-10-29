#include <catch2/catch_test_macros.hpp>
#include <future>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/speed_manager.hpp>
using namespace std::chrono_literals;
namespace linkerbot::hand::runtime {
    struct CallDrainTestAccess {
        static auto pin(CallDrain &calls, size_t expected) {
            auto state = calls.state_;
            std::unique_lock lock(state->mutex);
            const bool entered = state->cv.wait_for(lock, 1s, [&] { return state->calls == expected; });
            return std::make_pair(entered, std::move(lock));
        }
    };
}
namespace linkerbot::hand::o30 {
    struct ClientTestAccess {
        static auto pin(Client &client, size_t expected) {
            return runtime::CallDrainTestAccess::pin(client.calls_, expected);
        }
    };
}
TEST_CASE("comm: O30 close drains active and queued facade calls", "[comm][o30][runtime-migration]") {
    using namespace linkerbot::hand::o30;
    auto fake = std::make_unique<linkerbot::can::FakeTransport>();
    auto *raw = fake.get();
    linkerbot::can::CANDispatcher dispatcher(std::move(fake));
    Client client(dispatcher);
    SpeedManager speed(client);
    bool manager = false;
    SECTION("direct Client read") {}
    SECTION("real manager read and postprocessing") { manager = true; }
    std::promise<void> sent;
    raw->on_request(client.request_id(), [&](const auto &) {
        sent.set_value();
        return std::vector<linkerbot::can::Message>{};
    });
    auto query = [&] {
        try {
            if (manager) {
                (void)speed.get_blocking(5s);
            } else {
                (void)client.read(protocol::kMiSpeed, 0, 1, false, 5s);
            }
            return false;
        } catch (const std::exception &) {
            return true;
        }
    };
    auto active = std::async(std::launch::async, query);
    REQUIRE(sent.get_future().wait_for(1s) == std::future_status::ready);
    auto queued1 = std::async(std::launch::async, query);
    auto queued2 = std::async(std::launch::async, query);
    auto pinned = ClientTestAccess::pin(client, manager ? 4 : 3);
    std::promise<void> closing;
    auto closed = std::async(std::launch::async, [&] {
        closing.set_value();
        client.close();
    });
    closing.get_future().wait();
    auto barrier = closed.wait_for(30ms);
    pinned.second.unlock();
    closed.get();
    REQUIRE(pinned.first);
    REQUIRE(barrier == std::future_status::timeout);
    REQUIRE(active.get());
    REQUIRE(queued1.get());
    REQUIRE(queued2.get());
    REQUIRE(raw->sent().size() == 1);
    REQUIRE(client.runtime()->runtime_health().in_flight == 0);
}

TEST_CASE("comm: O30 close inside caller owned operation does not deadlock", "[comm][o30][runtime-migration]") {
    auto fake = std::make_unique<linkerbot::can::FakeTransport>();
    linkerbot::can::CANDispatcher dispatcher(std::move(fake));
    linkerbot::hand::o30::Client client(dispatcher);
    auto operation = client.operation(1s);
    REQUIRE_NOTHROW(client.close());
    REQUIRE_THROWS(client.read(2, 0, 1, false, 100ms));
}
