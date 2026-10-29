#include "hand_device_api_catalog.hpp"
#include "hand_device_transport_probe.hpp"
#include <catch2/catch_test_macros.hpp>
#include <future>
#include <limits>
#include <sstream>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

namespace {
    void require_motion_contracts() {
        if (env("HAND_DEVICE_ENABLE_MOTION_TESTS") != "1") {
            SKIP("set HAND_DEVICE_ENABLE_MOTION_TESTS=1 for stationary angle command contracts");
        }
    }
    void require_fault_contracts() {
        if (env("HAND_DEVICE_ENABLE_FAULT_TESTS") != "1") {
            SKIP("set HAND_DEVICE_ENABLE_FAULT_TESTS=1 for controlled transport fault injection");
        }
    }
    std::vector<float> percentages(const std::vector<int32_t> &raw, const DeviceCapabilities &caps) {
        std::vector<float> result;
        for (size_t i = 0; i < raw.size(); ++i) {
            REQUIRE(caps.joints[i].percent_mapping.has_value());
            const auto &map = *caps.joints[i].percent_mapping;
            const auto value = 100.0 * (raw[i] - map.raw_at_zero) / (map.raw_at_hundred - map.raw_at_zero);
            REQUIRE(value >= 0);
            REQUIRE(value <= 100);
            result.push_back(static_cast<float>(value));
        }
        return result;
    }
    void require_stationary(HandDevice &hand, const std::vector<int32_t> &pose, const std::vector<double> &before,
                            const HandDeviceConfig &config, o30::O30 *reader) {
        const auto sample = hand.get_angles(500ms);
        require_meta(sample.meta, hand.get_health().connection_generation);
        REQUIRE(sample.raw.size() == pose.size());
        const auto caps = hand.get_capabilities();
        REQUIRE(before.size() == pose.size());
        const auto configured = o30_setpoint_pose(config, reader);
        if (configured) {
            REQUIRE(*configured == pose);
        }
        for (size_t i = 0; i < pose.size(); ++i) {
            CAPTURE(i);
            const auto &range = caps.joints[i].raw_angle_range;
            const auto expected = configured ? before[i] : pose[i];
            REQUIRE(std::abs(sample.raw[i] - expected) <= std::max(0.5, (range.maximum - range.minimum) * 0.02));
        }
    }
}

TEST_CASE("HandDevice hardware constructor paths and zero IO caches", "[hardware][device][constructors][read-only]") {
    for (const auto shared : {false, true}) {
        DYNAMIC_SECTION((shared ? "shared Session" : "TransportFactory")) {
            ProbedFixture fixture(shared);
            auto &hand = *fixture.hand;
            hand.open();
            REQUIRE(hand.is_open());
            require_meta(hand.get_angles(500ms).meta, hand.get_health().connection_generation);
            const auto attempts = fixture.probe->attempts.load();
            REQUIRE(hand.get_capabilities().validate());
            REQUIRE(hand.get_health().connected());
            REQUIRE(hand.get_identity().has_value());
            REQUIRE(hand.get_verified_capabilities().has_value());
            REQUIRE(hand.get_snapshot().angles.has_value());
            REQUIRE(hand.is_open());
            REQUIRE(fixture.probe->attempts.load() == attempts);
            hand.close();
            REQUIRE_FALSE(hand.is_open());
            hand.open();
            REQUIRE(hand.is_open());
        }
    }
}

TEST_CASE("HandDevice hardware command returns before driver and wait does not resend",
          "[hardware][device][commands][motion][fault-injection]") {
    require_motion_contracts();
    require_fault_contracts();
    ProbedFixture fixture;
    auto &hand = *fixture.hand;
    hand.open();
    std::vector<double> measured;
    auto pose = stationary_pose(hand, fixture.config, &measured, fixture.get_setpoint_reader());
    const auto original = pose;
    fixture.probe->arm();
    const auto started = std::chrono::steady_clock::now();
    const auto handle = hand.set_angles(pose);
    REQUIRE(std::chrono::steady_clock::now() - started < 100ms);
    // Mutating caller storage after return must not alter the accepted target.
    for (auto &value : pose) {
        value = std::numeric_limits<int32_t>::max();
    }
    REQUIRE(fixture.probe->wait_entered());
    REQUIRE_FALSE(handle.done());
    REQUIRE(handle.stage() == CommandStage::kAccepted);
    const auto attempts = fixture.probe->attempts.load();
    REQUIRE_FALSE(handle.wait_for(10ms));
    REQUIRE_FALSE(handle.done());
    REQUIRE(fixture.probe->attempts.load() == attempts);
    REQUIRE_THROWS_AS(handle.wait_for(0ms), ValidationError);
    REQUIRE_THROWS_AS(handle.wait_for(-1ms), ValidationError);
    REQUIRE(hand.get_health().connected());
    fixture.probe->unblock();
    require_done(handle);
    const auto terminal_attempts = fixture.probe->attempts.load();
    const auto history = handle.history();
    REQUIRE(handle.wait() == handle.stage());
    REQUIRE(handle.wait_for(20ms));
    REQUIRE(fixture.probe->attempts.load() == terminal_attempts);
    REQUIRE(handle.history().size() == history.size());
    require_stationary(hand, original, measured, fixture.config, fixture.get_setpoint_reader());
}

TEST_CASE("HandDevice hardware raw and percent latest target replacement",
          "[hardware][device][commands][motion][fault-injection]") {
    require_motion_contracts();
    require_fault_contracts();
    ProbedFixture fixture;
    auto &hand = *fixture.hand;
    hand.open();
    std::vector<double> measured;
    const auto pose = stationary_pose(hand, fixture.config, &measured, fixture.get_setpoint_reader());
    fixture.probe->arm();
    const auto active = hand.set_angles(pose);
    REQUIRE(fixture.probe->wait_entered());
    const auto superseded = hand.set_angles(pose);
    const auto latest = hand.set_percent_angles(percentages(pose, hand.get_capabilities()));
    REQUIRE(superseded.wait_for(500ms));
    REQUIRE(superseded.stage() == CommandStage::kSuperseded);
    REQUIRE(superseded.error() == nullptr);
    REQUIRE(superseded.sent_frames() == 0);
    REQUIRE(latest.command_id() > superseded.command_id());
    REQUIRE(superseded.command_id() > active.command_id());
    fixture.probe->unblock();
    require_done(active);
    require_done(latest);
    REQUIRE(hand.get_health().superseded_commands() >= 1);
    require_stationary(hand, pose, measured, fixture.config, fixture.get_setpoint_reader());
}

TEST_CASE("HandDevice hardware injected send failure receipt", "[hardware][device][commands][fault-injection]") {
    require_fault_contracts();
    ProbedFixture fixture;
    auto &hand = *fixture.hand;
    hand.open();
    // No target reaches the physical link in this test.
    const auto caps = hand.get_capabilities();
    std::vector<int32_t> pose;
    for (const auto &joint : caps.joints) {
        pose.push_back(
            static_cast<int32_t>(std::round((joint.raw_angle_range.minimum + joint.raw_angle_range.maximum) / 2)));
    }
    const auto sent = fixture.probe->successful_writes().size();
    fixture.probe->reject_writes = true;
    const auto handle = hand.set_angles(pose);
    REQUIRE(handle.wait_for(2s));
    REQUIRE(handle.stage() == CommandStage::kFailed);
    REQUIRE(handle.sent_frames() == 0);
    REQUIRE(handle.error() != nullptr);
    REQUIRE_THROWS_AS(std::rethrow_exception(handle.error()), TransportError);
    REQUIRE(fixture.probe->successful_writes().size() == sent);
    const auto attempts = fixture.probe->attempts.load();
    REQUIRE(handle.wait_for(20ms));
    REQUIRE(fixture.probe->attempts.load() == attempts);
    fixture.probe->reject_writes = false;
    hand.recover();
    REQUIRE(hand.get_health().connected());
}

TEST_CASE("HandDevice hardware all numeric setters admit before driver IO",
          "[hardware][device][commands][fault-injection]") {
    require_fault_contracts();
    for (const auto &api : parameter_apis) {
        DYNAMIC_SECTION(api.name) {
            ProbedFixture fixture;
            auto &hand = *fixture.hand;
            hand.open();
            const auto caps = hand.get_capabilities();
            const auto *command = caps.command(api.semantic);
            if (!command) {
                REQUIRE_THROWS_AS((hand.*api.set)({}), UnsupportedOperationError);
                continue;
            }
            std::vector<double> values;
            for (size_t i = 0; i < command->channels.size(); ++i) {
                const auto &range = *command->range_for(i);
                auto midpoint = (range.minimum + range.maximum) / 2;
                if (range.integer_only) {
                    midpoint = std::round(midpoint);
                }
                REQUIRE(range.contains(midpoint));
                values.push_back(midpoint);
            }
            const auto writes = fixture.probe->successful_writes().size();
            fixture.probe->arm();
            const auto started = std::chrono::steady_clock::now();
            const auto handle = (hand.*api.set)(values);
            REQUIRE(std::chrono::steady_clock::now() - started < 100ms);
            values.assign(values.size(), std::numeric_limits<double>::quiet_NaN());
            REQUIRE(fixture.probe->wait_entered());
            REQUIRE_FALSE(handle.done());
            REQUIRE(handle.stage() == CommandStage::kAccepted);
            REQUIRE_FALSE(handle.wait_for(10ms));
            fixture.probe->unblock(true); // No configuration or torque reaches the device.
            REQUIRE(handle.wait_for(2s));
            REQUIRE(handle.stage() == CommandStage::kFailed);
            REQUIRE(handle.sent_frames() == 0);
            REQUIRE(handle.error() != nullptr);
            REQUIRE_THROWS_AS(std::rethrow_exception(handle.error()), TransportError);
            REQUIRE(fixture.probe->successful_writes().size() == writes);
        }
    }
}

TEST_CASE("HandDevice hardware latest parameter target replacement", "[hardware][device][commands][fault-injection]") {
    require_fault_contracts();
    for (const auto &api : parameter_apis) {
        if (api.semantic != CommandSemantic::kSpeeds && api.semantic != CommandSemantic::kCurrentLimits) {
            continue;
        }
        DYNAMIC_SECTION(api.name) {
            ProbedFixture fixture;
            auto &hand = *fixture.hand;
            hand.open();
            const auto caps = hand.get_capabilities();
            const auto *command = caps.command(api.semantic);
            if (!command) {
                REQUIRE_THROWS_AS((hand.*api.set)({}), UnsupportedOperationError);
                continue;
            }
            std::vector<double> values;
            for (size_t i = 0; i < command->channels.size(); ++i) {
                values.push_back(std::round((command->range_for(i)->minimum + command->range_for(i)->maximum) / 2));
            }
            const auto writes = fixture.probe->successful_writes().size();
            fixture.probe->arm();
            const auto active = (hand.*api.set)(values);
            REQUIRE(fixture.probe->wait_entered());
            const auto replaced = (hand.*api.set)(values);
            const auto latest = (hand.*api.set)(values);
            REQUIRE(replaced.wait_for(500ms));
            REQUIRE(replaced.stage() == CommandStage::kSuperseded);
            REQUIRE(replaced.sent_frames() == 0);
            REQUIRE(replaced.error() == nullptr);
            fixture.probe->reject_writes = true;
            fixture.probe->unblock(true);
            REQUIRE(active.wait_for(2s));
            REQUIRE(latest.wait_for(2s));
            REQUIRE(active.stage() == CommandStage::kFailed);
            REQUIRE(latest.stage() == CommandStage::kFailed);
            REQUIRE(fixture.probe->successful_writes().size() == writes);
        }
    }
}

TEST_CASE("HandDevice hardware missing required ACK times out", "[hardware][device][commands][fault-injection]") {
    require_fault_contracts();
    ProbedFixture fixture(false, 300ms);
    auto &hand = *fixture.hand;
    hand.open();
    const auto caps = hand.get_capabilities();
    std::function<CommandHandle()> send;
    bool synchronous = false;
    if (caps.operations.acknowledged(DeviceOperation::kDisable)) {
        synchronous = true;
        send = [&] { return hand.disable(); };
    } else if (caps.command(CommandSemantic::kAngles)->ack_support == AckSupport::kRequired) {
        require_motion_contracts();
        const auto pose = stationary_pose(hand, fixture.config, nullptr, fixture.get_setpoint_reader());
        send = [&, pose] { return hand.set_angles(pose); };
    } else {
        // Prefer common speed/current settings before advanced tuning. A
        // supplied baseline lets response-only protocols participate safely.
        for (const auto common : {true, false}) {
            for (const auto &api : parameter_apis) {
                const bool common_parameter =
                    api.semantic == CommandSemantic::kSpeeds || api.semantic == CommandSemantic::kCurrentLimits;
                if (common_parameter != common) {
                    continue;
                }
                const auto *command = caps.command(api.semantic);
                const auto *channel = caps.telemetry_channel(api.telemetry);
                if (!command || !channel || command->ack_support != AckSupport::kRequired) {
                    continue;
                }
                std::vector<double> values;
                if (channel->read_mode == ReadMode::kCommandCache || channel->read_mode == ReadMode::kCommandResponse) {
                    const auto known = env(api.baseline_env);
                    if (!known) {
                        continue;
                    }
                    auto text = *known;
                    std::replace(text.begin(), text.end(), ',', ' ');
                    std::istringstream stream(text);
                    double value;
                    while (stream >> value) {
                        values.push_back(value);
                    }
                    REQUIRE(stream.eof());
                } else {
                    const auto before = api.read(hand, 500ms);
                    if (!before.meta.valid) {
                        continue;
                    }
                    values = before.raw;
                }
                if (values.size() != command->channels.size()) {
                    continue;
                }
                bool legal = true;
                for (size_t i = 0; i < values.size(); ++i) {
                    legal &= command->range_for(i)->contains(values[i]);
                }
                if (!legal) {
                    continue;
                }
                send = [&, setter = api.set, values] { return (hand.*setter)(values); };
                break;
            }
            if (send) {
                break;
            }
        }
    }
    if (!send) {
        SKIP("provide a known baseline for a cached required-ACK command, or use a queried configuration");
    }
    fixture.probe->suppress_reads = true;
    if (synchronous) {
        // Discrete operations execute before returning a receipt; failure is
        // thrown at the call site. The nonblocking contract applies to set_*.
        const auto sent = fixture.probe->successful_writes().size();
        REQUIRE_THROWS_AS(send(), TimeoutError);
        REQUIRE(fixture.probe->successful_writes().size() > sent);
    } else {
        const auto handle = send();
        REQUIRE(handle.ack_expectation() == AckExpectation::kRequired);
        REQUIRE(handle.wait_for(2s));
        REQUIRE(handle.stage() == CommandStage::kFailed);
        REQUIRE(handle.sent_frames() > 0);
        REQUIRE(handle.error() != nullptr);
        REQUIRE_THROWS_AS(std::rethrow_exception(handle.error()), TimeoutError);
        const auto attempts = fixture.probe->attempts.load();
        REQUIRE(handle.wait_for(20ms));
        REQUIRE(fixture.probe->attempts.load() == attempts);
    }
    fixture.probe->suppress_reads = false;
    hand.recover();
    REQUIRE(hand.get_health().connected());
}

TEST_CASE("HandDevice hardware close and recover cancel pending targets without replay",
          "[hardware][device][commands][fault-injection]") {
    require_fault_contracts();
    for (const auto recover : {false, true}) {
        DYNAMIC_SECTION((recover ? "recover" : "close and reopen")) {
            ProbedFixture fixture;
            auto &hand = *fixture.hand;
            hand.open();
            const auto identity = *hand.get_identity();
            const auto before = hand.get_snapshot();
            std::vector<int32_t> target;
            for (const auto &joint : hand.get_capabilities().joints) {
                target.push_back(static_cast<int32_t>(
                    std::round((joint.raw_angle_range.minimum + joint.raw_angle_range.maximum) / 2)));
            }
            fixture.probe->arm();
            const auto active = hand.set_angles(target);
            REQUIRE(fixture.probe->wait_entered());
            const auto pending = hand.set_angles(target);
            auto shutdown = std::async(std::launch::async, [&] {
                if (recover) {
                    hand.recover();
                } else {
                    hand.close();
                }
            });
            const auto active_done = active.wait_for(500ms);
            const auto pending_done = pending.wait_for(500ms);
            // Always release a claimed driver call before checking assertions/future destruction.
            fixture.probe->unblock(true);
            REQUIRE(active_done);
            REQUIRE(pending_done);
            // The public close/recover contract terminates receipts as Failed
            // with ClosedError, rather than the scheduler's Cancelled state.
            REQUIRE(active.stage() == CommandStage::kFailed);
            REQUIRE(pending.stage() == CommandStage::kFailed);
            REQUIRE(active.error() != nullptr);
            REQUIRE(pending.error() != nullptr);
            REQUIRE_THROWS_AS(std::rethrow_exception(active.error()), ClosedError);
            REQUIRE_THROWS_AS(std::rethrow_exception(pending.error()), ClosedError);
            REQUIRE(active.sent_frames() == 0);
            REQUIRE(pending.sent_frames() == 0);
            REQUIRE(shutdown.wait_for(2s) == std::future_status::ready);
            REQUIRE_NOTHROW(shutdown.get());
            if (!recover) {
                hand.open();
            }
            require_identity_equal(*hand.get_identity(), identity);
            const auto after = hand.get_snapshot();
            if (recover) {
                REQUIRE(after.connection_generation > before.connection_generation);
            }
            REQUIRE(*after.angles->meta.collected_at > *before.angles->meta.collected_at);
            require_meta(after.angles->meta, after.connection_generation);
            REQUIRE(hand.get_health().pending_commands() == 0);
            const auto attempts = fixture.probe->attempts.load();
            std::this_thread::sleep_for(50ms);
            REQUIRE(fixture.probe->attempts.load() == attempts);
            const auto writes = fixture.probe->successful_writes();
            const auto blocked = *fixture.probe->blocked;
            for (const auto &message : writes) {
                REQUIRE_FALSE((message.id == blocked.id && message.dlc == blocked.dlc && message.data == blocked.data));
            }
            const auto snapshot = hand.get_snapshot();
            for (const auto *meta : {snapshot.configured_speeds ? &snapshot.configured_speeds->meta : nullptr,
                                     snapshot.current_limits ? &snapshot.current_limits->meta : nullptr}) {
                if (meta) {
                    REQUIRE(meta->connection_generation == after.connection_generation);
                }
            }
        }
    }
}
