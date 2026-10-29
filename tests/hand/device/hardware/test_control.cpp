#include "hand_device_api_catalog.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

namespace {
    struct DisableGuard {
        HandDevice &hand;
        bool active = false;
        ~DisableGuard() {
            if (!active) {
                return;
            }
            try {
                const auto handle = hand.disable();
                if (!handle.wait_for(2s) || handle.error() ||
                    (handle.stage() != CommandStage::kDispatched && handle.stage() != CommandStage::kAcknowledged)) {
                    std::cerr << "Control cleanup: disable did not succeed\n";
                }
            } catch (const std::exception &e) {
                std::cerr << "Control cleanup: " << e.what() << '\n';
            }
        }
    };
    void exercise_operation(DeviceOperation operation, const char *gate) {
        DeviceFixture fixture;
        auto &hand = fixture.device();
        hand.open();
        const auto caps = hand.get_capabilities();
        const OperationApi *api = nullptr;
        for (const auto &entry : operation_apis) {
            if (entry.semantic == operation) {
                api = &entry;
            }
        }
        REQUIRE(api != nullptr);
        if (!caps.supports(operation)) {
            REQUIRE_THROWS_AS((hand.*api->call)(), UnsupportedOperationError);
            return;
        }
        if (env(gate) != "1") {
            SKIP(std::string("set ") + gate + "=1 for " + api->name);
        }
        DisableGuard guard{hand};
        if (operation == DeviceOperation::kEnable) {
            REQUIRE(caps.supports(DeviceOperation::kDisable));
            guard.active = true;
        }
        const auto handle = (hand.*api->call)();
        REQUIRE(handle.family() == CommandFamily::kDiscreteOperation);
        REQUIRE(handle.ack_expectation() ==
                (caps.operations.acknowledged(operation) ? AckExpectation::kRequired : AckExpectation::kNone));
        require_done(handle);
        if (operation == DeviceOperation::kClearFaults) {
            const auto faults = hand.get_faults(500ms);
            require_meta(faults.meta, hand.get_health().connection_generation);
            for (const auto value : faults.raw) {
                REQUIRE(value == 0);
            }
        }
        if (guard.active) {
            require_done(hand.disable());
            guard.active = false;
        }
        REQUIRE(hand.get_health().connected());
        REQUIRE(hand.get_health().identity_verified);
        // Save ACK is delivery evidence; persistence across power cycles is a separate acceptance step.
    }
}

TEST_CASE("HandDevice hardware clear faults", "[hardware][device][control][write]") {
    exercise_operation(DeviceOperation::kClearFaults, "HAND_DEVICE_ENABLE_CONTROL_TESTS");
}
TEST_CASE("HandDevice hardware enable", "[hardware][device][control][write]") {
    exercise_operation(DeviceOperation::kEnable, "HAND_DEVICE_ENABLE_CONTROL_TESTS");
}
TEST_CASE("HandDevice hardware disable", "[hardware][device][control][write]") {
    exercise_operation(DeviceOperation::kDisable, "HAND_DEVICE_ENABLE_CONTROL_TESTS");
}
TEST_CASE("HandDevice hardware save parameters", "[hardware][device][control][persistent]") {
    exercise_operation(DeviceOperation::kSaveParameters, "HAND_DEVICE_ENABLE_SAVE_TESTS");
}
