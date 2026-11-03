#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>

#include "../../../src/hand/unified/detail/execution_gate.hpp"

#include <any>
#include <memory>
#include <new>
#include <vector>

using namespace linkerbot::hand;
using namespace linkerbot::hand::unified_detail;

namespace {
    template <class T> struct FailingAllocator {
        using value_type = T;
        FailingAllocator() = default;
        template <class U> FailingAllocator(const FailingAllocator<U> &) {}
        T *allocate(std::size_t) { throw std::bad_alloc{}; }
        void deallocate(T *, std::size_t) noexcept {}
    };
}

namespace linkerbot::hand::unified_detail {
    struct ExecutionGateTestAccess {
        static std::shared_ptr<void> fail_allocation(ExecutionGate &gate) {
            return gate.acquire_with_allocator(FailingAllocator<char>{}, true, runtime::Deadline::max(), {}, {});
        }
    };
}

TEST_CASE("execution gate allocation failure leaves the current lease and future admission intact", "[comm][gate]") {
    auto gate = std::make_shared<ExecutionGate>();
    auto incumbent = gate->acquire(false, runtime::Deadline::max());
    REQUIRE_THROWS_AS(ExecutionGateTestAccess::fail_allocation(*gate), std::bad_alloc);
    REQUIRE_THROWS_AS(gate->acquire(true, runtime::Clock::now()), TimeoutError);
    incumbent.reset();
    auto next = gate->acquire(true, runtime::deadline_after(std::chrono::seconds{1}));
    REQUIRE(next.get() == gate.get());
}

TEST_CASE("execution gate removes timed out cancelled and invalid waiters", "[comm][gate]") {
    auto gate = std::make_shared<ExecutionGate>();
    auto incumbent = gate->acquire(true, runtime::Deadline::max());
    REQUIRE_THROWS_AS(gate->acquire(false, runtime::Clock::now()), TimeoutError);
    runtime::CancellationSource cancellation;
    cancellation.cancel();
    REQUIRE_THROWS_AS(gate->acquire(false, runtime::Deadline::max(), cancellation.token()), runtime::CancelledError);
    REQUIRE_THROWS_AS(gate->acquire(false, runtime::Deadline::max(), {}, [] { return false; }), ClosedError);
    incumbent.reset();
    auto next = gate->acquire(false, runtime::deadline_after(std::chrono::seconds{1}));
    REQUIRE(next.get() == gate.get());
    std::weak_ptr<ExecutionGate> lifetime = gate;
    gate.reset();
    REQUIRE_FALSE(lifetime.expired());
    next.reset();
    REQUIRE(lifetime.expired());
}

TEST_CASE("checked optional test values own temporaries and reject missing data", "[comm][test-support]") {
    REQUIRE_THROWS_AS(test_support::require_value(std::optional<int>{}), std::logic_error);
    const auto value = test_support::require_value(std::optional<std::string>{"retained"});
    REQUIRE(value == "retained");
    const auto nested = std::optional<std::optional<int>>{std::optional<int>{7}};
    REQUIRE(test_support::require_value(test_support::require_value(nested)) == 7);
}

TEST_CASE("protocol progress retains and releases type-erased payload ownership", "[comm][ownership]") {
    auto owner = std::make_shared<int>(42);
    const std::weak_ptr<int> observed = owner;
    {
        auto progress = protocol::Progress::complete(std::vector<std::shared_ptr<int>>{owner});
        owner.reset();
        REQUIRE_FALSE(observed.expired());
        auto copy = progress;
        progress = protocol::Progress{};
        REQUIRE_FALSE(observed.expired());
        const auto &payload = std::any_cast<const std::vector<std::shared_ptr<int>> &>(copy.value);
        REQUIRE(payload.size() == 1);
        REQUIRE(*payload.front() == 42);
    }
    REQUIRE(observed.expired());
}
