#include "profile_helpers.hpp"
#include "profiles.hpp"

#include <array>

#include <linkerbot/hand/l20lite/joints.hpp>

namespace linkerbot::hand::capabilities_detail {
    namespace {
        DeviceCapabilities make_l20lite_profile(HandSide side) {
            constexpr std::array<JointMotionType, l20lite::kJointCount> motion_types = {
                JointMotionType::kFlexion,   JointMotionType::kRoll,      JointMotionType::kFlexion,
                JointMotionType::kFlexion,   JointMotionType::kFlexion,   JointMotionType::kFlexion,
                JointMotionType::kAbduction, JointMotionType::kAbduction, JointMotionType::kAbduction,
                JointMotionType::kRotation};

            auto joints = u8_joints(l20lite::kJointNames, motion_types);
            const auto names = channels(l20lite::kJointNames);
            const auto fingers = finger_channels();
            const auto u8 = u8_range();
            const auto acceleration = acceleration_u8_range();

            DeviceCapabilities profile;
            profile.model = Model::kL20Lite;
            profile.side = side;
            profile.transport = classic_can_transport(side);
            profile.operations.read_device_info = true;
            profile.joints = joints;
            profile.commands = {
                angle_command(joints, "u8", AckSupport::kUnsupported, true),
                command(CommandSemantic::kSpeeds, names, u8, "u8", "count",
                        "raw speed setting; physical scale is unverified", Confidence::kUnknown, AckSupport::kRequired),
                command(CommandSemantic::kAccelerations, names, acceleration, "u8", "count",
                        "raw acceleration setting; physical scale is unverified", Confidence::kUnknown,
                        AckSupport::kRequired),
                command(CommandSemantic::kCurrentLimits, names, u8, "u8", "count",
                        "raw current-limit setting; physical scale is unverified", Confidence::kUnknown,
                        AckSupport::kRequired),
                command(CommandSemantic::kStallDetectionTimes, names, u8, "u8", "count", "10 ms per count",
                        Confidence::kDocumented, AckSupport::kRequired),
                command(CommandSemantic::kStallThresholds, names, u8, "u8", "count", "raw percentage mapping",
                        Confidence::kUnknown, AckSupport::kRequired),
                command(CommandSemantic::kStallHoldCurrents, names, u8, "u8", "count",
                        "legacy stall-torque field exposed as hold current", Confidence::kConflicting,
                        AckSupport::kRequired),
            };
            profile.telemetry = {
                angle_telemetry(joints, "u8"),
                telemetry(TelemetrySemantic::kConfiguredSpeeds, names, ReadMode::kCommandResponse, "u8",
                          {l20lite::kJointCount}, u8, "count", "latest complete write response; no active query",
                          Confidence::kUnknown, false, false),
                telemetry(TelemetrySemantic::kConfiguredAccelerations, names, ReadMode::kCommandResponse, "u8",
                          {l20lite::kJointCount}, acceleration, "count",
                          "latest complete write response; no active query", Confidence::kUnknown, false, false),
                telemetry(TelemetrySemantic::kCurrentLimits, names, ReadMode::kCommandResponse, "u8",
                          {l20lite::kJointCount}, u8, "count", "latest complete write response; no active query",
                          Confidence::kUnknown, false, false),
                telemetry(TelemetrySemantic::kMeasuredSpeeds, names, ReadMode::kQuery, "u8", {l20lite::kJointCount}, u8,
                          "count", "real-time speed; physical scale is unverified", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kCurrents, names, ReadMode::kQuery, "u8", {l20lite::kJointCount}, u8,
                          "count", "real-time current, not the limit setting", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kTemperatures, names, ReadMode::kQuery, "u8", {l20lite::kJointCount}, u8,
                          "degree_celsius", "1 degree Celsius per count", Confidence::kDocumented),
                telemetry(TelemetrySemantic::kFaults, names, ReadMode::kQuery, "u8_bitmask", {l20lite::kJointCount}, u8,
                          std::nullopt, "per-joint fault bitmask", Confidence::kDocumented),
                telemetry(TelemetrySemantic::kForce, fingers, ReadMode::kQuery, "u8", {5, 12, 6}, u8, "count",
                          "raw tactile sensor count", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kForceFns, fingers, ReadMode::kQuery, "u8", {5}, u8, "count",
                          "normal-force scalar; physical scale is unverified", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kForceFts, fingers, ReadMode::kQuery, "u8", {5}, u8, "count",
                          "tangential-force scalar; physical scale is unverified", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kForceFds, fingers, ReadMode::kQuery, "u8", {5}, u8, "count",
                          "direction code; 255 means undefined", Confidence::kConflicting),
                telemetry(TelemetrySemantic::kForcePx, fingers, ReadMode::kQuery, "u8", {5}, u8, "count",
                          "raw proximity value", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kStallDetectionTimes, names, ReadMode::kCommandResponse, "u8",
                          {l20lite::kJointCount}, u8, "count", "latest complete write response; no active query",
                          Confidence::kDocumented, false, false),
                telemetry(TelemetrySemantic::kStallThresholds, names, ReadMode::kCommandResponse, "u8",
                          {l20lite::kJointCount}, u8, "count", "latest complete write response; no active query",
                          Confidence::kUnknown, false, false),
                telemetry(TelemetrySemantic::kStallHoldCurrents, names, ReadMode::kCommandResponse, "u8",
                          {l20lite::kJointCount}, u8, "count", "latest complete write response; no active query",
                          Confidence::kConflicting, false, false),
            };
            // Shared telemetry freshness contract with Python Hand SDK.
            for (auto &channel : profile.telemetry) {
                switch (channel.semantic) {
                case TelemetrySemantic::kStallThresholds:
                case TelemetrySemantic::kStallDetectionTimes:
                case TelemetrySemantic::kStallHoldCurrents:
                case TelemetrySemantic::kConfiguredSpeeds:
                case TelemetrySemantic::kCurrentLimits:
                case TelemetrySemantic::kConfiguredAccelerations:
                case TelemetrySemantic::kTemperatures:
                    channel.freshness_ms = 3000;
                    break;
                case TelemetrySemantic::kAngles:
                    channel.freshness_ms = 1000.0 / 30.0;
                    break;
                case TelemetrySemantic::kMeasuredSpeeds:
                case TelemetrySemantic::kCurrents:
                case TelemetrySemantic::kForce:
                case TelemetrySemantic::kForceFns:
                case TelemetrySemantic::kForceFts:
                case TelemetrySemantic::kForceFds:
                case TelemetrySemantic::kForcePx:
                    channel.freshness_ms = 100;
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

    const DeviceCapabilities &l20lite_profile(HandSide side) {
        return select_profile(side, [](HandSide selected) { return make_l20lite_profile(selected); });
    }

} // namespace linkerbot::hand::capabilities_detail
