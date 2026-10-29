#pragma once

#include <array>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <linkerbot/hand/capabilities.hpp>

namespace linkerbot::hand::capabilities_detail {
    inline NumericRange u8_range() { return {0.0, 255.0, true, 1.0}; }

    inline NumericRange acceleration_u8_range() { return {0.0, 254.0, true, 1.0}; }

    inline std::vector<std::string_view> finger_channels() { return {"thumb", "index", "middle", "ring", "pinky"}; }

    template <size_t N> std::vector<std::string_view> channels(const std::array<std::string_view, N> &names) {
        return {names.begin(), names.end()};
    }

    inline std::vector<std::string_view> channels(const std::vector<JointCapability> &joints) {
        std::vector<std::string_view> result;
        result.reserve(joints.size());
        for (const auto &joint : joints) {
            result.push_back(joint.name);
        }
        return result;
    }

    template <size_t N>
    std::vector<JointCapability> u8_joints(const std::array<std::string_view, N> &names,
                                           const std::array<JointMotionType, N> &motion_types,
                                           NumericRange raw_angle_range = u8_range()) {
        std::vector<JointCapability> joints;
        joints.reserve(N);
        for (size_t index = 0; index < N; ++index) {
            joints.push_back({index,
                              names[index],
                              motion_types[index],
                              raw_angle_range,
                              PercentMapping{raw_angle_range.minimum, raw_angle_range.maximum, "raw_min", "raw_max",
                                             Confidence::kMeasured},
                              {}});
        }
        return joints;
    }

    inline CommandCapability
    command(CommandSemantic semantic, const std::vector<std::string_view> &channel_names, NumericRange raw_range,
            std::string_view raw_encoding, std::optional<std::string_view> raw_unit = std::nullopt,
            std::optional<std::string_view> raw_scale_note = std::nullopt, Confidence confidence = Confidence::kUnknown,
            AckSupport ack_support = AckSupport::kUnsupported, bool returns_current_state = false) {
        CommandCapability result;
        result.semantic = semantic;
        result.channels = channel_names;
        result.raw_ranges = {raw_range};
        result.raw_encoding = raw_encoding;
        result.raw_unit = raw_unit;
        result.raw_scale_note = raw_scale_note;
        result.unit_confidence = confidence;
        result.ack_support = ack_support;
        result.returns_current_state = returns_current_state;
        result.normalized_ranges = {raw_range};
        result.normalized_unit = raw_unit;
        result.byte_order = raw_encoding.find("be") != std::string_view::npos   ? ByteOrder::kBigEndian
                            : raw_encoding.find("le") != std::string_view::npos ? ByteOrder::kLittleEndian
                                                                                : ByteOrder::kNotApplicable;
        result.signedness =
            raw_encoding.find("i16") != std::string_view::npos ? Signedness::kSigned : Signedness::kUnsigned;
        result.packing = raw_encoding.find("object") != std::string_view::npos ? Packing::kObject : Packing::kArray;
        result.write_mode =
            raw_encoding.find("sparse") != std::string_view::npos ? WriteMode::kSparse : WriteMode::kFull;
        result.timeout_ms = 250;
        result.response = returns_current_state                     ? ResponseSemantics::kCurrentState
                          : ack_support == AckSupport::kUnsupported ? ResponseSemantics::kNone
                                                                    : ResponseSemantics::kAck;
        result.idempotency = Idempotency::kIdempotent;
        return result;
    }

    inline CommandCapability angle_command(const std::vector<JointCapability> &joints, std::string_view raw_encoding,
                                           AckSupport ack_support = AckSupport::kUnsupported,
                                           bool returns_current_state = false) {
        CommandCapability result;
        result.semantic = CommandSemantic::kAngles;
        result.channels = channels(joints);
        result.raw_ranges.reserve(joints.size());
        for (const auto &joint : joints) {
            result.raw_ranges.push_back(joint.raw_angle_range);
        }
        result.raw_encoding = raw_encoding;
        result.raw_unit = "count";
        result.unit_confidence = Confidence::kMeasured;
        result.ack_support = ack_support;
        result.returns_current_state = returns_current_state;
        result.normalized_ranges.assign(joints.size(), NumericRange{0.0, 100.0, false, 0.01});
        result.normalized_unit = "percent";
        result.byte_order = raw_encoding.find("be") != std::string_view::npos   ? ByteOrder::kBigEndian
                            : raw_encoding.find("le") != std::string_view::npos ? ByteOrder::kLittleEndian
                                                                                : ByteOrder::kNotApplicable;
        result.signedness =
            raw_encoding.find("i16") != std::string_view::npos ? Signedness::kSigned : Signedness::kUnsigned;
        result.packing = raw_encoding.find("object") != std::string_view::npos ? Packing::kObject : Packing::kArray;
        result.write_mode =
            raw_encoding.find("sparse") != std::string_view::npos ? WriteMode::kSparse : WriteMode::kFull;
        result.timeout_ms = 250;
        result.response = returns_current_state                     ? ResponseSemantics::kCurrentState
                          : ack_support == AckSupport::kUnsupported ? ResponseSemantics::kNone
                                                                    : ResponseSemantics::kAck;
        result.idempotency = Idempotency::kIdempotent;
        result.percent_mappings.reserve(joints.size());
        for (const auto &joint : joints) {
            result.percent_mappings.push_back(joint.percent_mapping);
        }
        return result;
    }

    inline TelemetryCapability telemetry(TelemetrySemantic semantic, const std::vector<std::string_view> &channel_names,
                                         ReadMode read_mode, std::string_view raw_encoding, std::vector<size_t> shape,
                                         std::optional<NumericRange> raw_range,
                                         std::optional<std::string_view> raw_unit = std::nullopt,
                                         std::optional<std::string_view> raw_scale_note = std::nullopt,
                                         Confidence confidence = Confidence::kUnknown, bool device_measured = true,
                                         bool device_queried = true) {
        TelemetryCapability result;
        result.semantic = semantic;
        result.channels = channel_names;
        result.read_mode = read_mode;
        result.raw_encoding = raw_encoding;
        result.shape = std::move(shape);
        result.raw_range = raw_range;
        result.raw_unit = raw_unit;
        result.raw_scale_note = raw_scale_note;
        result.unit_confidence = confidence;
        result.device_measured = device_measured;
        result.device_queried = device_queried;
        result.source = device_measured ? TelemetrySource::kDevice : TelemetrySource::kCommandResponse;
        result.can_query = device_queried;
        result.can_report = read_mode == ReadMode::kReport || read_mode == ReadMode::kQueryAndReport;
        result.can_stream = result.can_report;
        if (result.can_report) {
            result.recommended_period_ms = 20;
            result.minimum_period_ms = 20;
            result.freshness_ms = 100;
        }
        return result;
    }

    inline TelemetryCapability angle_telemetry(const std::vector<JointCapability> &joints,
                                               std::string_view raw_encoding, ReadMode read_mode = ReadMode::kQuery) {
        return telemetry(TelemetrySemantic::kAngles, channels(joints), read_mode, raw_encoding, {joints.size()},
                         std::nullopt, "count", std::nullopt, Confidence::kMeasured);
    }

    inline TransportCapability classic_can_transport(HandSide side = HandSide::kUnknown) {
        TransportCapability result{TransportKind::kCan, 1000000U, std::nullopt, false,
                                   "Classic CAN: left endpoint 0x28, right endpoint 0x27"};
        if (side == HandSide::kLeft) {
            result.request_id = 0x28U;
        }
        if (side == HandSide::kRight) {
            result.request_id = 0x27U;
        }
        result.response_id = result.request_id;
        result.id_mask = 0x7FFU;
        result.mtu = 8;
        return result;
    }

    inline TransportCapability l30_transport() {
        TransportCapability result{TransportKind::kCanFd, 1000000U, 5000000U, true,
                                   "CAN FD extended frames: NodeID 1 and HostID 0 by default"};
        result.id_mask = 0x1FFFFFFFU;
        result.node_id = 1;
        result.host_id = 0;
        result.bit_rate_switch = true;
        result.mtu = 64;
        return result;
    }

    inline TransportCapability o30_transport() {
        TransportCapability result{TransportKind::kCanFd, 1000000U, std::nullopt, false,
                                   "CAN FD standard HOP frames without BRS: request 0x001, response 0x401 by default"};
        result.request_id = 0x001U;
        result.response_id = 0x401U;
        result.id_mask = 0x7FFU;
        result.bit_rate_switch = false;
        result.mtu = 64;
        return result;
    }

    template <typename Factory> const DeviceCapabilities &select_profile(HandSide side, Factory factory) {
        switch (side) {
        case HandSide::kUnknown: {
            static const auto profile = factory(HandSide::kUnknown);
            return profile;
        }
        case HandSide::kLeft: {
            static const auto profile = factory(HandSide::kLeft);
            return profile;
        }
        case HandSide::kRight: {
            static const auto profile = factory(HandSide::kRight);
            return profile;
        }
        }
        throw std::invalid_argument("invalid hand side");
    }

} // namespace linkerbot::hand::capabilities_detail
