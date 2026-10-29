#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>

#include "hand/o30/o30_fixture.hpp"

using namespace linkerbot::hand::o30;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
    constexpr uint8_t kProbe = 80;

    std::array<uint8_t, kJointCount> filled(uint8_t value) {
        std::array<uint8_t, kJointCount> values{};
        values.fill(value);
        return values;
    }

    struct RestoreTorques {
        O30 &hand;
        std::array<uint8_t, kJointCount> original;

        explicit RestoreTorques(O30 &h) : hand(h), original(h.torque.get_torque_limits(kTimeout).torque_limits) {}

        ~RestoreTorques() {
            try {
                hand.torque.set_torque_limits(original, kTimeout);
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
        }

        RestoreTorques(const RestoreTorques &) = delete;
        RestoreTorques &operator=(const RestoreTorques &) = delete;
        RestoreTorques(RestoreTorques &&) = delete;
        RestoreTorques &operator=(RestoreTorques &&) = delete;
    };
} // namespace

// 真机转矩。MI 0x06，20 路 0–255。读 kActual 是当前转矩（空闲可为 0）；
// 读 kSetpoint 才是刚下发的目标。

// 当前转矩应有 20 个值。
TEST_CASE("Torque: get_torques returns 20 actual values", "[o30][sensor][torque]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.torque.get_torques(kTimeout);
    REQUIRE(data.torques.size() == kJointCount);
}

// RTS 设定值也能读到 20 路。
TEST_CASE("Torque: get_torque_limits returns 20 values", "[o30][sensor][torque]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.torque.get_torque_limits(kTimeout);
    REQUIRE(data.torque_limits.size() == kJointCount);
}

// set_all_torque_limits 写统一转矩限制，读回应是刚下发的值。
TEST_CASE("Torque: set_all_torque_limits echoes on readback", "[o30][sensor][torque][configuration]") {
    auto &hand = test_util::o30_hand();
    const RestoreTorques restore(hand);

    REQUIRE_NOTHROW(hand.torque.set_all_torque_limits(kProbe, kTimeout));
    const auto data = hand.torque.get_torque_limits(kTimeout);
    REQUIRE(data.torque_limits == filled(kProbe));
}

// set_torque_limits 各关节不同，确认不是只能写同一值。
TEST_CASE("Torque: set_torque_limits per-joint echoes on readback", "[o30][sensor][torque][configuration]") {
    auto &hand = test_util::o30_hand();
    const RestoreTorques restore(hand);

    std::array<uint8_t, kJointCount> target{};
    for (size_t i = 0; i < kJointCount; ++i) {
        target[i] = static_cast<uint8_t>(30U + i * 5U);
    }
    REQUIRE_NOTHROW(hand.torque.set_torque_limits(target, kTimeout));
    REQUIRE(hand.torque.get_torque_limits(kTimeout).torque_limits == target);
}

// timeout<=0 该立刻抛，不落到总线上。
TEST_CASE("Torque: rejects non-positive timeout", "[o30][sensor][torque]") {
    auto &hand = test_util::o30_hand();
    REQUIRE_THROWS_AS(hand.torque.get_torques(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.torque.set_all_torque_limits(kProbe, std::chrono::milliseconds{0}),
                      linkerbot::hand::ValidationError);
}

// get_torques 之后，管理器和整机快照里 torque 都应有值。
TEST_CASE("Torque: snapshot populated after read", "[o30][sensor][torque]") {
    auto &hand = test_util::o30_hand();
    static_cast<void>(hand.torque.get_torques(kTimeout));
    REQUIRE(hand.torque.get_snapshot().has_value());
    REQUIRE(hand.get_snapshot().torque.has_value());
}
