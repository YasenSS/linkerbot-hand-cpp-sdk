#include "profile_helpers.hpp"
#include "profiles.hpp"

#include <array>

#include <linkerbot/hand/l20/joints.hpp>

namespace linkerbot::hand::capabilities_detail {
    namespace {
        DeviceCapabilities make_l20_profile(HandSide side) {
            constexpr std::array<JointMotionType, l20::kJointCount> motion_types = {
                JointMotionType::kRotation, JointMotionType::kRoll,      JointMotionType::kFlexion,
                JointMotionType::kFlexion,  JointMotionType::kAbduction, JointMotionType::kFlexion,
                JointMotionType::kFlexion,  JointMotionType::kAbduction, JointMotionType::kFlexion,
                JointMotionType::kFlexion,  JointMotionType::kAbduction, JointMotionType::kFlexion,
                JointMotionType::kFlexion,  JointMotionType::kAbduction, JointMotionType::kFlexion,
                JointMotionType::kFlexion};

            auto joints = u8_joints(l20::kJointNames, motion_types);
            const auto names = channels(l20::kJointNames);
            const auto u8 = u8_range();

            DeviceCapabilities profile;
            profile.model = Model::kL20;
            profile.side = side;
            profile.transport = classic_can_transport(side);
            profile.operations.read_device_info = true;
            profile.operations.clear_faults = true;
            profile.joints = joints;
            profile.commands = {
                angle_command(joints, "u8", AckSupport::kUnsupported, true),
                command(CommandSemantic::kSpeeds, names, u8, "u8", "count",
                        "raw speed setting; physical scale is unverified", Confidence::kUnknown,
                        AckSupport::kUnsupported),
                command(CommandSemantic::kCurrentLimits, names, u8, "u8", "count",
                        "raw current-limit setting; physical scale is unverified", Confidence::kUnknown,
                        AckSupport::kUnsupported),
            };
            profile.telemetry = {
                angle_telemetry(joints, "u8"),
                telemetry(TelemetrySemantic::kMeasuredSpeeds, names, ReadMode::kQuery, "u8", {l20::kJointCount}, u8,
                          "count", "real-time speed; physical scale is unverified", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kCurrents, names, ReadMode::kQuery, "u8", {l20::kJointCount}, u8, "count",
                          "real-time current, not the limit setting", Confidence::kUnknown),
                telemetry(TelemetrySemantic::kTemperatures, names, ReadMode::kQuery, "u8", {l20::kJointCount}, u8,
                          "degree_celsius", "1 degree Celsius per count", Confidence::kDocumented),
                telemetry(TelemetrySemantic::kFaults, names, ReadMode::kQuery, "u8_bitmask", {l20::kJointCount}, u8,
                          std::nullopt, "per-joint fault bitmask", Confidence::kDocumented),
                telemetry(TelemetrySemantic::kForce, finger_channels(), ReadMode::kQuery, "u8", {5, 12, 6}, u8, "count",
                          "raw tactile sensor count", Confidence::kUnknown),
            };
            // Shared telemetry freshness contract with Python Hand SDK.
            for (auto &channel : profile.telemetry) {
                switch (channel.semantic) {
                case TelemetrySemantic::kAngles:
                    channel.freshness_ms = 1000.0 / 30.0;
                    break;
                case TelemetrySemantic::kMeasuredSpeeds:
                case TelemetrySemantic::kCurrents:
                case TelemetrySemantic::kForce:
                    channel.freshness_ms = 100;
                    break;
                case TelemetrySemantic::kTemperatures:
                case TelemetrySemantic::kCurrentLimits:
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

    const DeviceCapabilities &l20_profile(HandSide side) {
        return select_profile(side, [](HandSide selected) { return make_l20_profile(selected); });
    }

} // namespace linkerbot::hand::capabilities_detail
