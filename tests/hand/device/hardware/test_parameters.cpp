#include "hand/support/optional_value.hpp"
#include "hand_device_api_catalog.hpp"
#include "hand_device_transport_probe.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

namespace {
    std::vector<double> baseline(const ParameterApi &api, const RawVectorSample &before,
                                 const TelemetryCapability *channel) {
        // A command cache is not a readable physical configuration, even if valid.
        if (channel && channel->read_mode != ReadMode::kCommandCache && before.meta.valid) {
            return before.raw;
        }
        const auto input = env(api.baseline_env);
        if (!input) {
            SKIP(std::string("provide known restoration baseline via ") + api.baseline_env +
                 " (comma-separated raw values); no device readback is available");
        }
        auto text = test_support::require_value(input);
        std::replace(text.begin(), text.end(), ',', ' ');
        std::istringstream stream(text);
        std::vector<double> result;
        double value = 0;
        while (stream >> value) {
            result.push_back(value);
        }
        REQUIRE(stream.eof());
        return result;
    }

    void checked_write(HandDevice &hand, const ParameterApi &api, const std::vector<double> &values) {
        const auto handle = (hand.*api.set)(values);
        if (!handle.wait_for(2s)) {
            throw std::runtime_error("parameter command did not terminate");
        }
        if (handle.error()) {
            std::rethrow_exception(handle.error());
        }
        const auto stage = handle.ack_expectation() == AckExpectation::kRequired ? CommandStage::kAcknowledged
                                                                                 : CommandStage::kDispatched;
        if (handle.stage() != stage || handle.sent_frames() == 0) {
            throw std::runtime_error("parameter command did not complete successfully");
        }
    }

    struct RestoreParameter {
        HandDevice &hand;
        const ParameterApi &api;
        std::vector<double> original;
        bool active = false;
        ~RestoreParameter() {
            if (!active) {
                return;
            }
            try {
                checked_write(hand, api, original);
            } catch (const std::exception &e) {
                std::cerr << "Parameter cleanup failed: " << api.name << ": " << e.what() << '\n';
            } catch (...) {
                std::cerr << "Parameter cleanup failed: " << api.name << '\n';
            }
        }
    };

    bool has_parameter_quantization(HandDevice &hand, const ParameterApi &api) {
        const auto identity = hand.get_identity();
        // Verified on L6 V2.4.5: current limits 254/250/240 read as
        // 253/249/239, and speeds 21/22/24 confirm as 20/23/23.
        // Retain the device feedback; account for its conversion in this test.
        const auto model = hand.get_capabilities().model;
        // V4.6.10 speed writes 21/24 reply as 20/23 on the wire;
        // 22/23/25 reply unchanged. Current-limit writes remain exact.
        if (model == Model::kL20Lite && api.semantic == CommandSemantic::kSpeeds && identity &&
            identity->firmware_version == "V4.6.10") {
            return true;
        }
        return model == Model::kL6 &&
               (api.semantic == CommandSemantic::kCurrentLimits || api.semantic == CommandSemantic::kSpeeds) &&
               identity && identity->firmware_version == "V2.4.5";
    }

    RawVectorSample require_values(HandDevice &hand, const ParameterApi &api, const CommandCapability &command,
                                   const TelemetryCapability &channel, const std::vector<double> &expected) {
        RawVectorSample last;
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        bool matched = false;
        bool confirmed_response = false;
        do {
            last = api.read(hand, 500ms);
            matched = last.meta.valid && last.raw.size() == expected.size();
            if (matched) {
                for (size_t i = 0; i < expected.size(); ++i) {
                    const auto *range = command.range_for(i);
                    const auto tolerance = has_parameter_quantization(hand, api) ? 1.0
                                           : range->integer_only                 ? 0.5
                                                                                 : 1e-6;
                    matched = matched && std::isfinite(last.raw[i]) && std::abs(last.raw[i] - expected[i]) <= tolerance;
                }
            }
            if (matched) {
                break;
            }
            if (channel.read_mode == ReadMode::kCommandResponse && last.meta.valid &&
                last.raw.size() == expected.size() && !confirmed_response) {
                // A completed write may return its pre-write value. With no
                // independent query, one identical completed write collects a
                // confirmation response. Never retry a timed-out command.
                checked_write(hand, api, expected);
                confirmed_response = true;
            }
            std::this_thread::sleep_for(20ms);
        } while (std::chrono::steady_clock::now() < deadline);
        CAPTURE(expected, last.raw);
        REQUIRE(matched);
        require_vector_sample(last, channel, hand.get_health().connection_generation);
        return last;
    }
}

TEST_CASE("HandDevice hardware parameter change and restoration", "[hardware][device][parameters][write]") {
    for (const auto &api : parameter_apis) {
        DYNAMIC_SECTION(api.name) {
            DeviceFixture fixture;
            auto &hand = fixture.device();
            hand.open();
            const auto caps = hand.get_capabilities();
            if (!caps.supports(api.semantic)) {
                REQUIRE_THROWS_AS((hand.*api.set)({}), UnsupportedOperationError);
                continue;
            }
            if (env("HAND_DEVICE_ENABLE_PARAMETER_TESTS") != "1") {
                SKIP("set HAND_DEVICE_ENABLE_PARAMETER_TESTS=1 for parameter changes");
            }
            const auto *command = caps.command(api.semantic);
            const auto *channel = caps.telemetry_channel(api.telemetry);
            REQUIRE(command != nullptr);
            RawVectorSample before;
            if (channel) {
                try {
                    before = api.read(hand, 500ms);
                } catch (const DeviceNotReadyError &) {
                    REQUIRE((channel->read_mode == ReadMode::kCommandCache ||
                             channel->read_mode == ReadMode::kCommandResponse));
                    REQUIRE(hand.is_open());
                }
            } else {
                REQUIRE_THROWS_AS(api.read(hand, 500ms), UnsupportedOperationError);
            }
            const auto original = baseline(api, before, channel);
            REQUIRE(original.size() == command->channels.size());
            for (size_t i = 0; i < original.size(); ++i) {
                REQUIRE(command->range_for(i) != nullptr);
                REQUIRE(command->range_for(i)->contains(original[i]));
            }
            RestoreParameter restore{hand, api, original};
            std::cout << "Parameter coverage: " << api.name << ", channels=" << original.size() << ", verification="
                      << (!channel                                           ? "transport-receipt-only"
                          : channel->read_mode == ReadMode::kCommandCache    ? "SDK-command-cache"
                          : channel->read_mode == ReadMode::kCommandResponse ? "device-write-response"
                                                                             : "device-query")
                      << '\n';
            restore.active = true;
            // Establish the explicitly supplied baseline for cache-only protocols.
            require_done((hand.*api.set)(original));
            if (channel) {
                (void)require_values(hand, api, *command, *channel, original);
            }
            for (size_t i = 0; i < original.size(); ++i) {
                CAPTURE(i);
                auto target = original;
                const auto &range = *command->range_for(i);
                auto step = range.integer_only ? std::max(1.0, range.step.value_or(1.0))
                                               : std::max(1e-6, (range.maximum - range.minimum) * 0.01);
                if (has_parameter_quantization(hand, api)) {
                    step = std::max(step, 3.0);
                    // One-count tolerance must not accept an unchanged value.
                    REQUIRE(step > 2.0);
                }
                target[i] += original[i] <= (range.minimum + range.maximum) / 2 ? step : -step;
                REQUIRE(range.contains(target[i]));
                REQUIRE(target[i] != original[i]);
                require_done((hand.*api.set)(target));
                if (channel) {
                    (void)require_values(hand, api, *command, *channel, target);
                }
                require_done((hand.*api.set)(original));
                if (channel) {
                    (void)require_values(hand, api, *command, *channel, original);
                }
            }
            restore.active = false;
        }
    }
}

TEST_CASE("HandDevice hardware configuration caches invalidate on recover",
          "[hardware][device][parameters][write][lifecycle]") {
    if (env("HAND_DEVICE_ENABLE_PARAMETER_TESTS") != "1") {
        SKIP("set HAND_DEVICE_ENABLE_PARAMETER_TESTS=1 and provide known baselines for cache invalidation");
    }
    for (const auto &api : parameter_apis) {
        DYNAMIC_SECTION(api.name) {
            ProbedFixture fixture;
            auto &hand = *fixture.hand;
            hand.open();
            const auto caps = hand.get_capabilities();
            const auto *command = caps.command(api.semantic);
            const auto *channel = caps.telemetry_channel(api.telemetry);
            if (!command || !channel ||
                (channel->read_mode != ReadMode::kCommandCache && channel->read_mode != ReadMode::kCommandResponse)) {
                INFO("cache invalidation applies to supported cached configuration, not queried telemetry");
                continue;
            }
            RawVectorSample before;
            try {
                before = api.read(hand, 500ms);
            } catch (const DeviceNotReadyError &) {
                REQUIRE(hand.is_open());
            }
            const auto original = baseline(api, before, channel);
            REQUIRE(original.size() == command->channels.size());
            for (size_t i = 0; i < original.size(); ++i) {
                REQUIRE(command->range_for(i)->contains(original[i]));
            }
            RestoreParameter restore{hand, api, original};
            restore.active = true;
            require_done((hand.*api.set)(original));
            const auto populated = require_values(hand, api, *command, *channel, original);
            const auto generation = hand.get_health().connection_generation;
            hand.recover();
            REQUIRE(hand.get_health().connection_generation > generation);
            try {
                const auto invalidated = api.read(hand, 500ms);
                REQUIRE_FALSE(invalidated.meta.valid);
                REQUIRE(invalidated.raw.empty());
                REQUIRE_FALSE(invalidated.meta.collected_at.has_value());
            } catch (const DeviceNotReadyError &) {
                REQUIRE(hand.is_open());
            }
            require_done((hand.*api.set)(original));
            const auto restored = require_values(hand, api, *command, *channel, original);
            REQUIRE(test_support::require_value(restored.meta.collected_at) >
                    test_support::require_value(populated.meta.collected_at));
            restore.active = false;
        }
    }
}
