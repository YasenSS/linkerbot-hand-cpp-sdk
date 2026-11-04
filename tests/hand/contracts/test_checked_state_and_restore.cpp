#include "../../../src/hand/detail/checked_optional.hpp"
#include "../../../src/hand/detail/failure_kind.hpp"
#include "restore_state.hpp"
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace {
    template <class Value, class = void> struct CanCheck : std::false_type {};
    template <class Value>
    struct CanCheck<Value,
                    std::void_t<decltype(linkerbot::hand::detail::checked_value(std::declval<Value>(), "state"))>>
        : std::true_type {};
}

TEST_CASE("checked runtime state preserves references and rejects empty or temporary state",
          "[comm][contracts][checked-state]") {
    using linkerbot::hand::detail::checked_value;
    static_assert(CanCheck<std::optional<int> &>::value);
    static_assert(CanCheck<const std::optional<int> &>::value);
    static_assert(!CanCheck<std::optional<int> &&>::value);
    static_assert(!CanCheck<const std::optional<int> &&>::value);
    std::optional<int> value{42};
    auto *storage = &value.value();
    auto &reference = checked_value(value, "missing state");
    REQUIRE(&reference == storage);
    reference = 7;
    REQUIRE(value == 7);
    const auto &read_only = value;
    REQUIRE(&checked_value(read_only, "missing state") == &reference);
    value.reset();
    REQUIRE_THROWS_AS(checked_value(value, "missing state"), linkerbot::hand::InternalError);
}

TEST_CASE("failure metrics retain dynamic error categories without changing causes",
          "[comm][contracts][error-metrics]") {
    using namespace linkerbot::hand;
    using detail::failure_kind;
    using detail::FailureKind;
    REQUIRE(failure_kind({}) == FailureKind::other);
    REQUIRE(failure_kind(std::make_exception_ptr(TimeoutError("timeout"))) == FailureKind::timeout);
    REQUIRE(failure_kind(std::make_exception_ptr(ClosedError("closed"))) == FailureKind::closed);
    REQUIRE(failure_kind(std::make_exception_ptr(LifecycleError("cancelled"))) == FailureKind::lifecycle);
    REQUIRE(failure_kind(std::make_exception_ptr(std::runtime_error("other"))) == FailureKind::other);
    REQUIRE(failure_kind(std::make_exception_ptr(42)) == FailureKind::other);
    // A base HandError carrying a timeout code did not match the old typed catch.
    REQUIRE(failure_kind(std::make_exception_ptr(HandError(ErrorCode::kTimeout, "base"))) == FailureKind::other);
}

TEST_CASE("benchmark restoration attempts both obligations and preserves the first failure",
          "[comm][contracts][restore]") {
    for (const bool pose_fails : {false, true}) {
        for (const bool reply_fails : {false, true}) {
            size_t poses = 0, replies = 0;
            std::string error;
            try {
                l6_benchmark::restore_state(
                    [&] {
                        ++poses;
                        if (pose_fails) {
                            throw std::runtime_error("pose");
                        }
                    },
                    [&] {
                        ++replies;
                        if (reply_fails) {
                            throw std::runtime_error("reply");
                        }
                    });
            } catch (const std::runtime_error &failure) {
                error = failure.what();
            }
            REQUIRE(poses == 1);
            REQUIRE(replies == 1);
            REQUIRE(error == (pose_fails ? "pose" : reply_fails ? "reply" : ""));
        }
    }
    bool reply_attempted = false;
    REQUIRE_THROWS_AS(l6_benchmark::restore_state([] { throw 42; }, [&] { reply_attempted = true; }), int);
    REQUIRE(reply_attempted);
}
