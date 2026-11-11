#include "profile_helpers.hpp"
#include "profiles.hpp"

#include <array>

#include <linkerbot/hand/o30/joints.hpp>

namespace linkerbot::hand::capabilities_detail {
    namespace {
        DeviceCapabilities make_o30_profile(HandSide side) {
            constexpr std::array<JointMotionType, o30::kJointCount> motion_types = {
                JointMotionType::kRotation,  JointMotionType::kRoll,      JointMotionType::kAbduction,
                JointMotionType::kAbduction, JointMotionType::kAbduction, JointMotionType::kAbduction,
                JointMotionType::kFlexion,   JointMotionType::kFlexion,   JointMotionType::kFlexion,
                JointMotionType::kFlexion,   JointMotionType::kFlexion,   JointMotionType::kFlexion,
                JointMotionType::kFlexion,   JointMotionType::kFlexion,   JointMotionType::kFlexion,
                JointMotionType::kFlexion,   JointMotionType::kFlexion,   JointMotionType::kFlexion,
                JointMotionType::kFlexion,   JointMotionType::kFlexion};

            auto joints = u8_joints(o30::kJointNames, motion_types);
            for (size_t index = 0; index < joints.size(); ++index) {
                const auto &semantics = o30::kJointSemantics[index];
                const auto flexion = motion_types[index] == JointMotionType::kFlexion;
                joints[index].percent_mapping = PercentMapping{
                    flexion ? 255.0 : 0.0, flexion ? 0.0 : 255.0, flexion ? semantics.max_pose : semantics.min_pose,
                    flexion ? semantics.min_pose : semantics.max_pose, Confidence::kMeasured};
            }
            const auto names = channels(o30::kJointNames);
            const auto u8 = u8_range();
            constexpr auto encoding = "u8_v25_object";

            DeviceCapabilities profile;
            profile.model = Model::kO30;
            profile.side = side;
            profile.transport = o30_transport();
            profile.operations.read_device_info = true;
            profile.joints = joints;
            profile.commands = {
                angle_command(joints, encoding, AckSupport::kRequired, true),
                command(CommandSemantic::kSpeeds, names, u8, encoding, "count", std::nullopt, Confidence::kUnknown,
                        AckSupport::kRequired),
                command(CommandSemantic::kAccelerations, names, u8, encoding, "count", std::nullopt,
                        Confidence::kUnknown, AckSupport::kRequired),
                command(CommandSemantic::kCurrentLimits, names, u8, encoding, "count",
                        "MI 0x05 setpoint, separate from measured current", Confidence::kUnknown,
                        AckSupport::kRequired),
                command(CommandSemantic::kStallDetectionTimes, names, u8, encoding, std::nullopt, std::nullopt,
                        Confidence::kUnknown, AckSupport::kRequired),
                command(CommandSemantic::kStallThresholds, names, u8, encoding, std::nullopt, std::nullopt,
                        Confidence::kUnknown, AckSupport::kRequired),
                command(CommandSemantic::kStallHoldCurrents, names, u8, encoding, std::nullopt, std::nullopt,
                        Confidence::kUnknown, AckSupport::kRequired),
            };
            profile.telemetry = {
                angle_telemetry(joints, encoding),
                telemetry(TelemetrySemantic::kMeasuredSpeeds, names, ReadMode::kQuery, encoding, {o30::kJointCount}, u8,
                          "count", "MI 0x03 actual value", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kConfiguredSpeeds, names, ReadMode::kQuery, encoding, {o30::kJointCount},
                          u8, "count", "MI 0x03 setpoint", Confidence::kUnknown, false),
                telemetry(TelemetrySemantic::kConfiguredAccelerations, names, ReadMode::kQuery, encoding,
                          {o30::kJointCount}, u8, "count", "MI 0x04 setpoint", Confidence::kUnknown, false),
                telemetry(TelemetrySemantic::kCurrents, names, ReadMode::kQuery, encoding, {o30::kJointCount}, u8,
                          "count", "MI 0x05 actual value", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kCurrentLimits, names, ReadMode::kQuery, encoding, {o30::kJointCount}, u8,
                          "count", "MI 0x05 setpoint", Confidence::kUnknown, false),
                telemetry(TelemetrySemantic::kTorques, names, ReadMode::kQuery, encoding, {o30::kJointCount}, u8,
                          std::nullopt, "MI 0x06 actual value", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kTemperatures, names, ReadMode::kQuery, encoding, {o30::kJointCount}, u8,
                          "count", std::nullopt, Confidence::kUnknown),
                telemetry(TelemetrySemantic::kVoltages, names, ReadMode::kQuery, encoding, {o30::kJointCount}, u8,
                          "count", std::nullopt, Confidence::kUnknown),
                telemetry(TelemetrySemantic::kFaults, names, ReadMode::kQuery, "u8_bitmask_v25_object",
                          {o30::kJointCount}, u8, std::nullopt, "per-joint fault bitmask", Confidence::kDocumented),
                telemetry(TelemetrySemantic::kForce, finger_channels(), ReadMode::kQuery, "o30_tactile_descriptor", {5},
                          std::nullopt, std::nullopt, "runtime sensor metadata defines unit and shape",
                          Confidence::kDocumented),
                telemetry(TelemetrySemantic::kStallDetectionTimes, names, ReadMode::kQuery, encoding,
                          {o30::kJointCount}, u8, std::nullopt, std::nullopt, Confidence::kUnknown, false),
                telemetry(TelemetrySemantic::kStallThresholds, names, ReadMode::kQuery, encoding, {o30::kJointCount},
                          u8, std::nullopt, std::nullopt, Confidence::kUnknown, false),
                telemetry(TelemetrySemantic::kStallHoldCurrents, names, ReadMode::kQuery, encoding, {o30::kJointCount},
                          u8, std::nullopt, std::nullopt, Confidence::kUnknown, false),
            };
            // Shared telemetry freshness contract with Python Hand SDK.
            for (auto &channel : profile.telemetry) {
                switch (channel.semantic) {
                case TelemetrySemantic::kAngles:
                    channel.freshness_ms = 1000.0 / 15.0;
                    break;
                case TelemetrySemantic::kMeasuredSpeeds:
                case TelemetrySemantic::kConfiguredSpeeds:
                case TelemetrySemantic::kCurrentLimits:
                    channel.freshness_ms = 100;
                    break;
                case TelemetrySemantic::kConfiguredAccelerations:
                case TelemetrySemantic::kForce:
                case TelemetrySemantic::kCurrents:
                case TelemetrySemantic::kTorques:
                    channel.freshness_ms = 300;
                    break;
                case TelemetrySemantic::kTemperatures:
                case TelemetrySemantic::kVoltages:
                case TelemetrySemantic::kStallDetectionTimes:
                case TelemetrySemantic::kStallThresholds:
                case TelemetrySemantic::kStallHoldCurrents:
                    channel.freshness_ms = 3000;
                    break;
                case TelemetrySemantic::kFaults:
                    channel.freshness_ms = 500;
                    break;
                default:
                    break;
                }
            }
            return profile;
        }
    }

    const DeviceCapabilities &o30_profile(HandSide side) {
        return select_profile(side, [](HandSide selected) { return make_o30_profile(selected); });
    }

} // namespace linkerbot::hand::capabilities_detail
