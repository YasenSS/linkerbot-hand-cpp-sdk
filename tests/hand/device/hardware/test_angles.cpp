#include "hand/support/optional_value.hpp"
#include "hand_device_angle_baseline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

namespace {
    struct MotionOptions {
        double step_percent;
        double tolerance_percent;
        double normalize_max_percent;
        std::chrono::milliseconds settle_timeout;
    };

    double positive_option(const char *name, double fallback) {
        const auto value = env(name);
        if (!value) {
            return fallback;
        }
        size_t consumed = 0;
        const auto parsed = std::stod(*value, &consumed);
        if (consumed != value->size() || !std::isfinite(parsed) || parsed <= 0) {
            throw std::invalid_argument(std::string{name} + " must be finite and positive");
        }
        return parsed;
    }

    MotionOptions motion_options() {
        const auto step = positive_option("HAND_DEVICE_ANGLE_STEP_PERCENT", 5.0);
        const auto tolerance = positive_option("HAND_DEVICE_ANGLE_TOLERANCE_PERCENT", 2.0);
        const auto normalize_max = positive_option("HAND_DEVICE_ANGLE_NORMALIZE_MAX_PERCENT", 20.0);
        const auto timeout = positive_option("HAND_DEVICE_ANGLE_SETTLE_MS", 5000.0);
        REQUIRE(step <= 10.0);
        REQUIRE(step > 2.0 * tolerance);
        REQUIRE(normalize_max <= 100.0);
        REQUIRE(timeout >= 1.0);
        REQUIRE(timeout <= 60000.0);
        return {step, tolerance, normalize_max, std::chrono::milliseconds{static_cast<int64_t>(timeout)}};
    }

    void complete(const CommandHandle &handle) {
        if (!handle.wait_for(2s)) {
            throw std::runtime_error("angle command did not terminate");
        }
        if (const auto error = handle.error()) {
            std::rethrow_exception(error);
        }
        const auto expected = handle.ack_expectation() == AckExpectation::kRequired ? CommandStage::kAcknowledged
                                                                                    : CommandStage::kDispatched;
        if (handle.stage() != expected || !handle.done() || handle.sent_frames() == 0) {
            throw std::runtime_error("angle command has no successful dispatch/ACK receipt");
        }
    }

    void require_sample(const AngleSample &sample, const DeviceCapabilities &caps, uint64_t generation) {
        REQUIRE(sample.meta.valid);
        REQUIRE_FALSE(sample.meta.stale);
        REQUIRE(sample.meta.collected_at.has_value());
        REQUIRE(sample.meta.origin == TelemetryValueOrigin::kDeviceMeasured);
        REQUIRE(sample.meta.connection_generation == generation);
        REQUIRE(sample.raw.size() == caps.joints.size());
        REQUIRE(sample.percent.size() == caps.joints.size());
        REQUIRE(sample.physical_rad.size() == caps.joints.size());
        for (size_t index = 0; index < caps.joints.size(); ++index) {
            CAPTURE(index);
            REQUIRE(std::isfinite(sample.raw[index]));
            REQUIRE(sample.percent[index].has_value() == caps.joints[index].percent_mapping.has_value());
            if (sample.percent[index]) {
                REQUIRE(std::isfinite(test_support::require_value(sample.percent[index])));
            }
            REQUIRE(sample.physical_rad[index].has_value() == caps.joints[index].physical_angle_mapping.has_value());
            if (sample.physical_rad[index]) {
                REQUIRE(std::isfinite(test_support::require_value(sample.physical_rad[index])));
            }
        }
        // Feedback may overshoot: do not clamp it to the command range.
    }

    // Sensor feedback is preserved in AngleSample. Only the command target is
    // bounded, since an out-of-envelope measurement cannot be sent back.
    std::vector<int32_t> command_pose(const AngleSample &sample, const DeviceCapabilities &caps) {
        std::vector<int32_t> result;
        for (size_t index = 0; index < sample.raw.size(); ++index) {
            const auto &range = caps.joints[index].raw_angle_range;
            const auto minimum = std::ceil(range.minimum);
            const auto maximum = std::floor(range.maximum);
            CAPTURE(index, minimum, maximum);
            REQUIRE(minimum <= maximum);
            REQUIRE(minimum >= std::numeric_limits<int32_t>::min());
            REQUIRE(maximum <= std::numeric_limits<int32_t>::max());
            const auto target = std::clamp(std::round(sample.raw[index]), minimum, maximum);
            REQUIRE(range.contains(target));
            result.push_back(static_cast<int32_t>(target));
        }
        return result;
    }

    double raw_tolerance(const JointCapability &joint, double percent) {
        return std::max(0.5, (joint.raw_angle_range.maximum - joint.raw_angle_range.minimum) * percent / 100.0);
    }

    AngleSample wait_for_pose(HandDevice &hand, const DeviceCapabilities &caps, const std::vector<int32_t> &target,
                              const MotionOptions &options, std::optional<size_t> selected_joint = std::nullopt) {
        const auto generation = hand.get_health().connection_generation;
        const auto deadline = std::chrono::steady_clock::now() + options.settle_timeout;
        unsigned settled = 0;
        do {
            auto sample = hand.get_angles(500ms);
            if (!sample.meta.valid || sample.meta.stale || sample.meta.connection_generation != generation ||
                sample.raw.size() != target.size()) {
                throw std::runtime_error("angle readback is invalid, stale, or from another generation");
            }
            bool reached = true;
            for (size_t index = 0; index < target.size(); ++index) {
                if (selected_joint && index != *selected_joint) {
                    continue;
                }
                if (!std::isfinite(sample.raw[index]) ||
                    std::abs(sample.raw[index] - target[index]) >
                        raw_tolerance(caps.joints[index], options.tolerance_percent)) {
                    reached = false;
                }
            }
            settled = reached ? settled + 1 : 0;
            if (settled == 2) {
                return sample;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                for (size_t index = 0; index < target.size(); ++index) {
                    std::cerr << "joint=" << index << ", target_raw=" << target[index]
                              << ", observed_raw=" << sample.raw[index] << '\n';
                }
                break;
            }
            std::this_thread::sleep_for(20ms);
        } while (true);
        throw std::runtime_error("angle feedback did not settle within HAND_DEVICE_ANGLE_SETTLE_MS");
    }

    // Normal restoration is checked; assertion/exception unwinding also makes
    // a best-effort restoration and disables models with an enable operation.
    class PoseGuard {
    public:
        PoseGuard(HandDevice &hand, const DeviceCapabilities &caps, std::vector<int32_t> original,
                  MotionOptions options, std::optional<std::vector<int32_t>> measured_original = std::nullopt)
            : hand_(hand), caps_(caps), original_(std::move(original)), options_(options),
              measured_original_(std::move(measured_original)) {}

        ~PoseGuard() {
            if (moved_) {
                try {
                    restore();
                } catch (const std::exception &error) {
                    std::cerr << "Angle cleanup: pose restoration failed: " << error.what() << '\n';
                } catch (...) {
                    std::cerr << "Angle cleanup: pose restoration failed\n";
                }
            }
            if (enabled_) {
                try {
                    finish();
                } catch (const std::exception &error) {
                    std::cerr << "Angle cleanup: disable failed: " << error.what() << '\n';
                } catch (...) {
                    std::cerr << "Angle cleanup: disable failed\n";
                }
            }
        }

        void enable() {
            if (!caps_.supports(DeviceOperation::kEnable)) {
                return;
            }
            REQUIRE(caps_.supports(DeviceOperation::kDisable));
            enabled_ = true;
            complete(hand_.enable());
        }

        void moving(std::optional<size_t> selected_joint = std::nullopt) {
            moved_ = true;
            selected_joint_ = selected_joint;
        }

        AngleSample restore() {
            complete(hand_.set_angles(original_));
            // O30 can have a pre-existing target/feedback offset. Restore the
            // original target but verify return to the measured starting pose.
            auto restored = wait_for_pose(hand_, caps_, measured_original_ ? *measured_original_ : original_, options_,
                                          selected_joint_);
            moved_ = false;
            selected_joint_.reset();
            return restored;
        }

        void finish() {
            if (enabled_) {
                complete(hand_.disable());
                enabled_ = false;
            }
        }

    private:
        HandDevice &hand_;
        const DeviceCapabilities &caps_;
        std::vector<int32_t> original_;
        MotionOptions options_;
        std::optional<std::vector<int32_t>> measured_original_;
        bool moved_ = false;
        bool enabled_ = false;
        std::optional<size_t> selected_joint_;
    };

    void exercise_motion(bool percent_command, bool all_joints = false) {
        const auto enabled = env("HAND_DEVICE_ENABLE_MOTION_TESTS");
        if (!enabled || *enabled != "1") {
            SKIP("set HAND_DEVICE_ENABLE_MOTION_TESTS=1 for angle motion tests");
        }
        const auto options = motion_options();
        DeviceFixture fixture;
        const auto configured_pose = o30_setpoint_pose(fixture.config(), fixture.setpoint_reader());
        auto &hand = fixture.device();
        hand.open();
        const auto caps = hand.get_capabilities();
        REQUIRE(caps.supports(CommandSemantic::kAngles));
        REQUIRE_FALSE(caps.joints.empty());
        auto before = hand.get_angles(500ms);
        require_sample(before, caps, hand.get_health().connection_generation);
        const auto original = configured_pose ? *configured_pose : command_pose(before, caps);
        REQUIRE(original.size() == caps.joints.size());
        if (configured_pose) {
            for (size_t index = 0; index < original.size(); ++index) {
                CAPTURE(index, before.raw[index], original[index]);
                REQUIRE(caps.joints[index].raw_angle_range.contains(original[index]));
                // A large pre-existing tracking error makes a full-vector
                // command unsafe even when its other targets are unchanged.
                REQUIRE(std::abs(before.raw[index] - original[index]) <= raw_tolerance(caps.joints[index], 10.0));
            }
        }
        size_t first = 0, end = caps.joints.size();
        const auto selected = env("HAND_DEVICE_ANGLE_JOINT");
        if (selected && !all_joints) {
            REQUIRE_FALSE(selected->empty());
            REQUIRE(std::all_of(selected->begin(), selected->end(),
                                [](char digit) { return digit >= '0' && digit <= '9'; }));
            const auto parsed = std::stoull(*selected);
            REQUIRE(parsed < end);
            first = static_cast<size_t>(parsed);
            end = first + 1;
        }
        std::cout << "Angle coverage: mode=" << (percent_command ? "percent" : "raw") << ", joints=" << end - first
                  << '/' << caps.joints.size() << '\n';
        if (selected && all_joints) {
            std::cout << "Full acceptance ignores HAND_DEVICE_ANGLE_JOINT=" << *selected << '\n';
        }
        bool adjusted_start = false;
        std::vector<size_t> excessive_corrections;
        for (size_t index = 0; index < original.size(); ++index) {
            const auto &joint = caps.joints[index];
            if (joint.raw_angle_range.contains(std::round(before.raw[index]))) {
                continue;
            }
            adjusted_start = true;
            // Initial pose preparation is independent of the later test step.
            // Gather every outlier before rejecting, so one retry does not
            // merely reveal the next out-of-range joint.
            const auto correction = std::abs(before.raw[index] - original[index]);
            const auto limit = std::max(0.5, (joint.raw_angle_range.maximum - joint.raw_angle_range.minimum) *
                                                 options.normalize_max_percent / 100.0);
            if (correction > limit) {
                excessive_corrections.push_back(index);
            }
            std::cerr << "Initial feedback outside command range: joint=" << index << " (" << joint.name
                      << "), feedback_raw=" << before.raw[index] << ", baseline_target=" << original[index]
                      << ", range=[" << joint.raw_angle_range.minimum << ", " << joint.raw_angle_range.maximum
                      << "], correction_raw=" << correction << ", correction_limit_raw=" << limit << '\n';
        }
        if (adjusted_start) {
            INFO("Set HAND_DEVICE_ANGLE_NORMALIZE_START=1 to allow an initial correction; "
                 "the corrected pose becomes the restoration target");
            REQUIRE(env("HAND_DEVICE_ANGLE_NORMALIZE_START") == "1");
            INFO("Initial correction is limited by HAND_DEVICE_ANGLE_NORMALIZE_MAX_PERCENT; "
                 "feedback tolerance and test step remain unchanged");
            CAPTURE(options.normalize_max_percent, excessive_corrections);
            REQUIRE(excessive_corrections.empty());
        }
        std::optional<std::vector<int32_t>> measured_original;
        if (configured_pose) {
            measured_original = command_pose(before, caps);
        }
        PoseGuard guard(hand, caps, original, options, measured_original);
        guard.enable();
        if (adjusted_start) {
            guard.moving();
            complete(hand.set_angles(original));
            before = wait_for_pose(hand, caps, original, options);
            require_sample(before, caps, hand.get_health().connection_generation);
        }
        for (size_t index = first; index < end; ++index) {
            CAPTURE(index, caps.joints[index].name, percent_command);
            auto target = original;
            const auto &joint = caps.joints[index];
            const auto &range = joint.raw_angle_range;
            double percent_target = 0;
            std::vector<float> percentages;
            if (percent_command) {
                // Derive legal percent targets from the command baseline, not
                // from sensor percentages that may legitimately exceed 0..100.
                for (size_t channel = 0; channel < original.size(); ++channel) {
                    REQUIRE(caps.joints[channel].percent_mapping.has_value());
                    const auto &mapping = test_support::require_value(caps.joints[channel].percent_mapping);
                    REQUIRE(mapping.raw_at_hundred != mapping.raw_at_zero);
                    const auto value = 100.0 * (original[channel] - mapping.raw_at_zero) /
                                       (mapping.raw_at_hundred - mapping.raw_at_zero);
                    REQUIRE(value >= 0.0);
                    REQUIRE(value <= 100.0);
                    percentages.push_back(static_cast<float>(value));
                }
                percent_target = percentages[index] <= 50.0 ? percentages[index] + options.step_percent
                                                            : percentages[index] - options.step_percent;
                percentages[index] = static_cast<float>(percent_target);
                REQUIRE(joint.percent_mapping.has_value());
                const auto &mapping = test_support::require_value(joint.percent_mapping);
                REQUIRE(mapping.quantization == Quantization::kNearestTiesAwayFromZero);
                target[index] = static_cast<int32_t>(std::llround(
                    mapping.raw_at_zero + percentages[index] / 100.0 * (mapping.raw_at_hundred - mapping.raw_at_zero)));
            } else {
                const auto step =
                    std::max(1.0, std::round((range.maximum - range.minimum) * options.step_percent / 100.0));
                target[index] +=
                    static_cast<int32_t>(original[index] <= (range.minimum + range.maximum) / 2.0 ? step : -step);
            }
            REQUIRE(range.contains(target[index]));
            // An unmoving hand must not pass just because its target is close.
            REQUIRE(std::abs(static_cast<double>(target[index]) - original[index]) >
                    2.0 * raw_tolerance(joint, options.tolerance_percent));
            std::cout << "Angle motion: joint=" << index << " (" << joint.name << "), baseline_raw=" << original[index]
                      << ", target_raw=" << target[index] << ", mode=" << (percent_command ? "percent" : "raw") << '\n';
            // O30's public full-vector command may shift other axes whose
            // measured angles already differ from their configured targets.
            // This case verifies the selected axis; the loop tests each axis.
            const auto selected_joint = caps.model == Model::kO30 ? std::optional<size_t>{index} : std::nullopt;
            guard.moving(selected_joint);
            complete(percent_command ? hand.set_percent_angles(percentages) : hand.set_angles(target));
            if (configured_pose) {
                const auto written_targets = o30_setpoint_pose(fixture.config(), fixture.setpoint_reader());
                REQUIRE(written_targets.has_value());
                REQUIRE(test_support::require_value(written_targets) == target);
                std::cout << "O30 setpoint confirmed: joint=" << index << ", target_raw=" << target[index] << '\n';
            }
            const auto after = wait_for_pose(hand, caps, target, options, selected_joint);
            require_sample(after, caps, hand.get_health().connection_generation);
            REQUIRE(test_support::require_value(after.meta.collected_at) >
                    test_support::require_value(before.meta.collected_at));
            if (percent_command) {
                REQUIRE(after.percent[index].has_value());
                const auto &mapping = test_support::require_value(joint.percent_mapping);
                const auto quantization = 50.0 / std::abs(mapping.raw_at_hundred - mapping.raw_at_zero);
                REQUIRE(std::abs(test_support::require_value(after.percent[index]) - percent_target) <=
                        options.tolerance_percent + quantization);
            }
            const auto restored = guard.restore();
            if (configured_pose) {
                const auto restored_targets = o30_setpoint_pose(fixture.config(), fixture.setpoint_reader());
                REQUIRE(restored_targets.has_value());
                REQUIRE(test_support::require_value(restored_targets) == original);
                for (size_t other = 0; other < original.size(); ++other) {
                    if (other == index) {
                        continue;
                    }
                    CAPTURE(other);
                    const auto limit = raw_tolerance(caps.joints[other], 10.0);
                    REQUIRE(std::abs(after.raw[other] - before.raw[other]) <= limit);
                    REQUIRE(std::abs(restored.raw[other] - before.raw[other]) <= limit);
                }
            }
        }
        guard.finish();
        std::cout << "Angle coverage complete: mode=" << (percent_command ? "percent" : "raw")
                  << ", joints=" << end - first << '/' << caps.joints.size() << '\n';
    }
}

namespace {
    void check_angle_readback() {
        DeviceFixture fixture;
        auto &hand = fixture.device();
        hand.open();
        const auto caps = hand.get_capabilities();
        REQUIRE(caps.supports(TelemetrySemantic::kAngles));
        REQUIRE_FALSE(caps.joints.empty());
        const auto first = hand.get_angles(500ms);
        const auto second = hand.get_angles(500ms);
        const auto generation = hand.get_health().connection_generation;
        require_sample(first, caps, generation);
        require_sample(second, caps, generation);
        REQUIRE(test_support::require_value(second.meta.collected_at) >
                test_support::require_value(first.meta.collected_at));
    }

    void check_angle_input_validation() {
        DeviceFixture fixture;
        auto &hand = fixture.device();
        hand.open();
        const auto caps = hand.get_capabilities();
        REQUIRE(caps.supports(CommandSemantic::kAngles));
        REQUIRE_FALSE(caps.joints.empty());
        // Invalid-input checks need a legal vector, not an in-range physical pose.
        std::vector<int32_t> raw;
        for (const auto &joint : caps.joints) {
            const auto midpoint = std::round((joint.raw_angle_range.minimum + joint.raw_angle_range.maximum) / 2.0);
            REQUIRE(joint.raw_angle_range.contains(midpoint));
            REQUIRE(midpoint >= std::numeric_limits<int32_t>::min());
            REQUIRE(midpoint <= std::numeric_limits<int32_t>::max());
            raw.push_back(static_cast<int32_t>(midpoint));
        }
        const auto count = raw.size();
        for (const auto size : {size_t{0}, count - 1, count + 1}) {
            CAPTURE(size);
            REQUIRE_THROWS_AS(hand.set_angles(std::vector<int32_t>(size)), ValidationError);
            REQUIRE_THROWS_AS(hand.set_percent_angles(std::vector<float>(size)), ValidationError);
        }
        for (size_t index = 0; index < count; ++index) {
            CAPTURE(index);
            const auto &range = caps.joints[index].raw_angle_range;
            for (const auto value : {std::floor(range.minimum) - 1.0, std::ceil(range.maximum) + 1.0}) {
                REQUIRE(value >= std::numeric_limits<int32_t>::min());
                REQUIRE(value <= std::numeric_limits<int32_t>::max());
                auto invalid = raw;
                invalid[index] = static_cast<int32_t>(value);
                REQUIRE_THROWS_AS(hand.set_angles(invalid), ValidationError);
            }
            for (const auto value : {-0.01F, 100.01F, std::numeric_limits<float>::quiet_NaN(),
                                     std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
                auto invalid = std::vector<float>(count, 50.0F);
                invalid[index] = value;
                REQUIRE_THROWS_AS(hand.set_percent_angles(invalid), ValidationError);
            }
        }
        REQUIRE_THROWS_AS(hand.get_angles(0ms), ValidationError);
        REQUIRE_THROWS_AS(hand.get_angles(-1ms), ValidationError);
    }

    void check_angle_open_state() {
        DeviceFixture fixture;
        auto &hand = fixture.device();
        REQUIRE_THROWS_AS(hand.set_angles({}), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.set_percent_angles({}), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.get_angles(500ms), DeviceNotReadyError);
        hand.open();
        hand.close();
        REQUIRE_THROWS_AS(hand.set_angles({}), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.set_percent_angles({}), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.get_angles(500ms), DeviceNotReadyError);
    }
}

TEST_CASE("HandDevice hardware angle readback", "[hardware][device][angles][read-only]") { check_angle_readback(); }

TEST_CASE("HandDevice hardware raw angle commands", "[hardware][device][angles][motion]") { exercise_motion(false); }

TEST_CASE("HandDevice hardware percent angle commands", "[hardware][device][angles][motion]") { exercise_motion(true); }

TEST_CASE("HandDevice hardware angle input validation", "[hardware][device][angles][validation]") {
    check_angle_input_validation();
}

TEST_CASE("HandDevice hardware angle operations require open", "[hardware][device][angles][validation]") {
    check_angle_open_state();
}

TEST_CASE("HandDevice hardware full angle acceptance", "[hardware][device][angles][acceptance][all-joints]") {
    if (env("HAND_DEVICE_ENABLE_MOTION_TESTS") != "1") {
        SKIP("set HAND_DEVICE_ENABLE_MOTION_TESTS=1 for full angle acceptance");
    }
    std::cout << "Full angle acceptance: closed-state checks\n";
    check_angle_open_state();
    std::cout << "Full angle acceptance: fresh angle readback\n";
    check_angle_readback();
    std::cout << "Full angle acceptance: invalid-input checks\n";
    check_angle_input_validation();
    std::cout << "Full angle acceptance: raw commands for every joint\n";
    exercise_motion(false, true);
    std::cout << "Full angle acceptance: percent commands for every joint\n";
    exercise_motion(true, true);
}
