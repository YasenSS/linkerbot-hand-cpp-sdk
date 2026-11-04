#include <algorithm>
#include <cmath>
#include <linkerbot/hand/l6/hop_l6.hpp>
#include <linkerbot/hand/l6/joints.hpp>
#include <linkerbot/hand/protocol/tactile.hpp>
#ifdef __linux__
#include <linkerbot/can/socketcan_transport.hpp>
#endif

namespace linkerbot::hand::l6 {
    namespace {
        std::unique_ptr<can::CANTransport> make_transport(std::string_view name) {
#ifdef __linux__
            return std::make_unique<can::SocketCANTransport>(name, true);
#else
            (void)name;
            throw TransportError("HopL6 requires an explicit CAN FD Transport on this platform");
#endif
        }
        DeviceCapabilities profile(Side side, uint32_t id) {
            if (side != Side::kLeft && side != Side::kRight) {
                throw ValidationError("invalid L6 side");
            }
            HandDeviceConfig config;
            config.model = Model::kL6;
            config.side = side == Side::kLeft ? HandSide::kLeft : HandSide::kRight;
            config.l6_protocol = L6Protocol::kHopV004;
            config.address = id;
            return declared_capabilities(config);
        }
        // UTF-8 fixed-width strings, terminated by NUL. Reject malformed encodings,
        // including overlong sequences, surrogate codepoints and values above U+10FFFF.
        std::string text(const std::vector<uint8_t> &data, size_t offset, size_t size) {
            if (offset + size > data.size()) {
                throw ProtocolError("short L6 HOP text field");
            }
            std::string result;
            for (size_t i = offset; i < offset + size && data[i];) {
                uint32_t cp = data[i];
                const size_t width = cp < 0x80                  ? 1
                                     : cp >= 0xc2 && cp <= 0xdf ? 2
                                     : cp >= 0xe0 && cp <= 0xef ? 3
                                     : cp >= 0xf0 && cp <= 0xf4 ? 4
                                                                : 0;
                if (!width || i + width > offset + size) {
                    throw ProtocolError("invalid HOP UTF-8 text");
                }
                cp &= width == 1 ? 0x7f : (1U << (7 - width)) - 1;
                for (size_t j = 1; j < width; ++j) {
                    if ((data[i + j] & 0xc0) != 0x80) {
                        throw ProtocolError("invalid HOP UTF-8 continuation");
                    }
                    cp = (cp << 6) | (data[i + j] & 0x3f);
                }
                if ((width == 2 && cp < 0x80) || (width == 3 && cp < 0x800) || (width == 4 && cp < 0x10000) ||
                    cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
                    throw ProtocolError("invalid HOP UTF-8 codepoint");
                }
                result.append(reinterpret_cast<const char *>(data.data() + i), width);
                i += width;
            }
            const auto first = result.find_first_not_of(" \t\r\n");
            return first == std::string::npos ? std::string{}
                                              : result.substr(first, result.find_last_not_of(" \t\r\n") - first + 1);
        }
        template <class T>
        std::optional<T> vector_snapshot(const std::shared_ptr<runtime::DeviceRuntime> &core, const char *domain) {
            const auto value = core->snapshot_as<RawVectorSample>(domain);
            if (!value) {
                return {};
            }
            T result;
            static_cast<RawVectorSample &>(result) = *value;
            return result;
        }
        AngleSample angles(const std::vector<uint8_t> &bytes, SampleMeta meta) {
            AngleSample result;
            result.meta = std::move(meta);
            for (const auto raw : hop::project_positions(bytes)) {
                result.raw.push_back(raw);
                result.percent.push_back(raw * 100.0 / 65535.0);
                result.physical_rad.emplace_back();
            }
            return result;
        }
    }
    HopL6::HopL6(Side side, std::string_view name, uint32_t id) : HopL6(side, make_transport(name), id) {}
    HopL6::HopL6(Side side, std::unique_ptr<can::CANTransport> transport, uint32_t id)
        : HopL6(side, transport::Session::own(std::move(transport)), id) {
        owns_session_ = true;
    }
    HopL6::HopL6(Side side, std::shared_ptr<transport::Session> session, uint32_t id)
        : session_(std::move(session)), runtime_(runtime::DeviceRuntime::acquire(
                                            session_, "l6/hop/" + std::to_string(id), {{hop::response_id(id), false}})),
          request_id_(id), capabilities_(profile(side, id)), angle(*this),
          speed(*this, 0x02, true, "speeds", "speed_targets"),
          current_limit(*this, 0x04, true, "current_limits", "current_limits"), current(*this, 0x04, false, "currents"),
          temperature(*this, 0x07, false, "temperatures"), fault(*this), force_sensor(*this), version(*this),
          config(*this) {
        for (const auto *channel :
             {"angles", "speeds", "current_limits", "currents", "temperatures", "faults", "force", "version"}) {
            runtime_->register_channel(channel);
        }
        for (const auto *family : {"angle_targets", "speed_targets", "current_limits", "configuration"}) {
            runtime_->register_command_family(family);
        }
    }
    HopL6::~HopL6() {
        try {
            stop();
        } catch (...) {
            try {
                runtime_->quiesce_for_destruction();
                calls_.drain();
                // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
                // NOLINTNEXTLINE(bugprone-empty-catch)
            } catch (...) {
            }
        }
    }
    runtime::DeviceRuntime::Operation HopL6::operation(std::chrono::milliseconds timeout) {
        auto call = calls_.hold();
        auto result = runtime_->operation(runtime::deadline_after(timeout));
        result.retain_call(std::move(call));
        return result;
    }
    runtime::Sample HopL6::execute(runtime::TransactionPlan plan, runtime::Deadline deadline) {
        return runtime_->execute(std::move(plan), deadline);
    }
    std::vector<uint8_t> HopL6::read_range(uint8_t mi, uint8_t si, size_t length, bool rts,
                                           runtime::Deadline deadline) {
        if (!length || size_t(si) + length > 256) {
            throw ValidationError("invalid L6 HOP register range");
        }
        std::vector<uint8_t> result;
        while (result.size() < length) {
            const auto count = static_cast<uint8_t>(std::min<size_t>(61, length - result.size()));
            auto sample = execute(
                hop::transaction(request_id_, mi, static_cast<uint8_t>(si + result.size()), count, {}, rts), deadline);
            const auto &part = std::any_cast<const std::vector<uint8_t> &>(sample.value);
            result.insert(result.end(), part.begin(), part.end());
        }
        return result;
    }
    SampleMeta HopL6::metadata(const runtime::Sample &sample, const char *domain, bool configured) const {
        SampleMeta result;
        // Composite reads/writes have independent wire domains. Use the business
        // channel's publication sequence so switching from query to command
        // feedback never makes a public sample's sequence go backwards.
        const auto previous = runtime_->snapshot(domain);
        result.sequence = previous ? previous->sequence + 1 : 1;
        result.collected_at = sample.received_at;
        result.valid = sample.valid;
        result.stale = !sample.valid;
        result.connection_generation = sample.device_generation;
        result.origin = configured ? TelemetryValueOrigin::kDeviceConfigured : TelemetryValueOrigin::kDeviceMeasured;
        result.delivery = sample.origin == hand::protocol::Origin::command_feedback
                              ? TelemetryDelivery::kCommandResponse
                              : TelemetryDelivery::kQueryResponse;
        return result;
    }
    void HopL6::publish(std::string domain, HandEvent event, runtime::Sample provenance) {
        std::visit([&](const auto &value) { runtime_->publish(domain, value, provenance); }, event.sample);
        const std::lock_guard<std::mutex> lock(event_mutex_);
        if (events_) {
            events_->push(event);
        }
    }
    AngleSample HopL6::AngleManager::get_blocking(std::chrono::milliseconds timeout) {
        auto op = owner_->operation(timeout);
        auto bytes = owner_->read_range(0x20, 10, 20, false, op.deadline());
        auto provenance = owner_->runtime_->response_provenance();
        auto result = angles(bytes, owner_->metadata(provenance, "angles"));
        owner_->publish("angles", {TelemetrySemantic::kAngles, result}, provenance);
        return result;
    }
    void HopL6::AngleManager::set_angles(const std::array<uint16_t, 6> &values, std::chrono::milliseconds timeout) {
        auto op = owner_->operation(timeout);
        std::vector<uint8_t> bytes;
        for (auto value : values) {
            hand::protocol::hop::append_u16_le(bytes, value);
        }
        auto sample = owner_->execute(hop::write_joints(owner_->request_id_, 0x20, bytes, true, false, "angle_targets"),
                                      op.deadline());
        if (sample.valid) {
            auto result = angles(std::any_cast<std::vector<uint8_t>>(sample.value), owner_->metadata(sample, "angles"));
            owner_->publish("angles", {TelemetrySemantic::kAngles, result}, sample);
        }
    }
    void HopL6::AngleManager::set_angles_percent(const std::array<float, 6> &values,
                                                 std::chrono::milliseconds timeout) {
        std::array<uint16_t, 6> raw{};
        for (size_t i = 0; i < raw.size(); ++i) {
            if (!std::isfinite(values[i]) || values[i] < 0 || values[i] > 100) {
                throw ValidationError("L6 HOP percent must be 0..100");
            }
            raw[i] = static_cast<uint16_t>(std::lround(values[i] * 65535.0 / 100.0));
        }
        set_angles(raw, timeout);
    }
    std::optional<AngleSample> HopL6::AngleManager::get_snapshot() const {
        return owner_->runtime_->snapshot_as<AngleSample>("angles");
    }
    RawVectorSample HopL6::VectorManager::get_blocking(std::chrono::milliseconds timeout) {
        auto op = owner_->operation(timeout);
        const auto raw = hop::project_bytes(owner_->read_range(mi_, 5, 10, configured_, op.deadline()));
        const auto provenance = owner_->runtime_->response_provenance();
        RawVectorSample result{{raw.begin(), raw.end()}, owner_->metadata(provenance, domain_, configured_)};
        // RawVectorSample is stored directly; public event payloads use the semantic type.
        owner_->runtime_->publish(domain_, result, provenance);
        HandEvent event;
        if (mi_ == 2) {
            ConfiguredSpeedSample typed;
            static_cast<RawVectorSample &>(typed) = result;
            event = {TelemetrySemantic::kConfiguredSpeeds, typed};
        } else if (configured_) {
            CurrentLimitSample typed;
            static_cast<RawVectorSample &>(typed) = result;
            event = {TelemetrySemantic::kCurrentLimits, typed};
        } else if (mi_ == 4) {
            CurrentSample typed;
            static_cast<RawVectorSample &>(typed) = result;
            event = {TelemetrySemantic::kCurrents, typed};
        } else {
            TemperatureSample typed;
            static_cast<RawVectorSample &>(typed) = result;
            event = {TelemetrySemantic::kTemperatures, typed};
        }
        const std::lock_guard<std::mutex> lock(owner_->event_mutex_);
        if (owner_->events_) {
            owner_->events_->push(event);
        }
        return result;
    }
    void HopL6::VectorManager::set_values(const std::array<uint8_t, 6> &values, std::chrono::milliseconds timeout) {
        if (!family_) {
            throw UnsupportedOperationError("this L6 HOP channel is read-only");
        }
        auto op = owner_->operation(timeout);
        auto sample = owner_->execute(
            hop::write_joints(owner_->request_id_, mi_, {values.begin(), values.end()}, false, true, family_),
            op.deadline());
        if (sample.valid) {
            const auto raw = hop::project_bytes(std::any_cast<std::vector<uint8_t>>(sample.value));
            RawVectorSample result{{raw.begin(), raw.end()}, owner_->metadata(sample, domain_, true)};
            owner_->runtime_->publish(domain_, result, sample);
            HandEvent event;
            if (mi_ == 2) {
                ConfiguredSpeedSample typed;
                static_cast<RawVectorSample &>(typed) = result;
                event = {TelemetrySemantic::kConfiguredSpeeds, typed};
            } else {
                CurrentLimitSample typed;
                static_cast<RawVectorSample &>(typed) = result;
                event = {TelemetrySemantic::kCurrentLimits, typed};
            }
            const std::lock_guard<std::mutex> lock(owner_->event_mutex_);
            if (owner_->events_) {
                owner_->events_->push(event);
            }
        }
    }
    std::optional<RawVectorSample> HopL6::VectorManager::get_snapshot() const {
        return owner_->runtime_->snapshot_as<RawVectorSample>(domain_);
    }
    FaultSample HopL6::FaultManager::get_blocking(std::chrono::milliseconds timeout) {
        auto op = owner_->operation(timeout);
        const auto raw = hop::project_bytes(owner_->read_range(0x0c, 5, 10, false, op.deadline()));
        auto provenance = owner_->runtime_->response_provenance();
        FaultSample result;
        result.raw.assign(raw.begin(), raw.end());
        for (auto value : raw) {
            result.statuses.push_back(decode_fault_status(value, hop::kFaultMask));
        }
        result.meta = owner_->metadata(provenance, "faults");
        owner_->publish("faults", {TelemetrySemantic::kFaults, result}, provenance);
        return result;
    }
    std::optional<FaultSample> HopL6::FaultManager::get_snapshot() const {
        return owner_->runtime_->snapshot_as<FaultSample>("faults");
    }
    DeviceIdentity HopL6::VersionManager::get_blocking(std::chrono::milliseconds timeout) {
        auto op = owner_->operation(timeout);
        const auto blob = owner_->read_range(0x41, 0, 249, false, op.deadline());
        const auto model = text(blob, 0, 16), uid = text(blob, 0x39, 32), name = text(blob, 0x59, 8);
        const auto upper_model = identity_detail::upper(model);
        if (upper_model != "L6" && upper_model.rfind("LHL6-", 0) != 0 && upper_model.rfind("L6-", 0) != 0) {
            throw IdentityMismatchError("HOP product model is not L6: " + model);
        }
        const auto protocol_version = text(blob, 0x61, 8), app = text(blob, 0xa1, 8);
        if (identity_detail::upper(name) != "HOP" || protocol_version.empty() || uid.empty() || app.empty()) {
            throw IdentityProtocolError("L6 HOP identity requires protocol name/version, device UID and app version");
        }
        const auto reported = identity_detail::parse_side(text(blob, 0xf1, 8));
        const auto expected =
            owner_->capabilities_.side == HandSide::kLeft ? IdentitySide::kLeft : IdentitySide::kRight;
        if (!reported) {
            throw IdentityProtocolError("L6 HOP identity does not report a valid side");
        }
        if (*reported != expected) {
            throw IdentityMismatchError("L6 HOP reported side does not match configuration");
        }
        DeviceIdentity result;
        result.model = Model::kL6;
        result.side = owner_->capabilities_.side;
        result.address = owner_->request_id_;
        result.product_code = model;
        const auto parts = identity_detail::split_product(uid);
        if (parts.size() >= 4 &&
            (identity_detail::upper(parts[0]) == "LHL6" || identity_detail::upper(parts[0]) == "L6")) {
            const auto side = identity_detail::parse_side(parts[3]);
            if (side && side != reported) {
                throw IdentityMismatchError("L6 UID and reported side disagree");
            }
            result.product_code = uid;
            result.serial_number = parts[2];
        }
        result.device_uid = uid;
        result.firmware_version = app;
        result.protocol_name = name;
        result.protocol_version = protocol_version;
        result.hardware_version = text(blob, 0x89, 16);
        result.mechanical_version = text(blob, 0xa9, 8);
        const auto provenance = owner_->runtime_->response_provenance();
        result.timestamp = provenance.received_at;
        owner_->runtime_->publish("version", result, provenance);
        return result;
    }
    std::optional<DeviceIdentity> HopL6::VersionManager::get_snapshot() const {
        return owner_->runtime_->snapshot_as<DeviceIdentity>("version");
    }
    void HopL6::ConfigManager::save(std::chrono::milliseconds timeout) {
        auto op = owner_->operation(timeout);
        (void)owner_->execute(hop::transaction(owner_->request_id_, 0x35, 0x1e, 1, {1}, false, "configuration"),
                              op.deadline());
    }
    ForceSample HopL6::ForceSensorManager::get_blocking(std::chrono::milliseconds timeout) {
        auto op = owner_->operation(timeout); // selection + metadata + all banks are one exclusive operation
        o30::SensorData round;
        for (uint8_t index = 1; index <= 5; ++index) {
            const auto ack = owner_->execute(
                hop::transaction(owner_->request_id_, 0x31, 0x6e, 1, {index}, false, "sensor_selection"),
                op.deadline());
            if (std::any_cast<std::vector<uint8_t>>(ack.value) != std::vector<uint8_t>{index}) {
                throw ProtocolError("L6 HOP sensor selection was not acknowledged");
            }
            const auto blob = owner_->read_range(0x31, 0, 115, false, op.deadline());
            o30::SensorReading reading;
            auto &info = reading.info;
            info.type = text(blob, 0, 32);
            info.unit = text(blob, 32, 8);
            info.data_range = blob[40];
            info.maximum_rows = blob[41];
            info.maximum_columns = blob[42];
            const auto descriptor_length = blob[43];
            if (descriptor_length > 64) {
                throw ProtocolError("L6 sensor descriptor exceeds its register range");
            }
            info.descriptor = text(blob, 44, descriptor_length);
            if (info.descriptor.size() != descriptor_length) {
                throw ProtocolError("L6 sensor descriptor length does not match its metadata");
            }
            info.total_length = hand::protocol::hop::read_u16_le(blob.data() + 108);
            info.selected = blob[110];
            info.rows = blob[111];
            info.columns = blob[112];
            info.flags = static_cast<uint16_t>(blob[113] | uint16_t(blob[114]) << 8);
            if (info.selected != index || info.total_length > 765 || blob[113] > 1 || blob[114] > 1 ||
                info.rows > info.maximum_rows || info.columns > info.maximum_columns) {
                throw ProtocolError("invalid L6 HOP sensor metadata");
            }
            if (blob[113]) {
                throw ProtocolError("L6 HOP sensor is in validity-mask mode, not measurement mode");
            }
            if ((info.type == "NO_SENSOR" || info.type.empty()) && info.total_length) {
                throw ProtocolError("missing L6 HOP sensor reports nonempty data");
            }
            if (!info.total_length && !info.descriptor.empty()) {
                throw ProtocolError("empty L6 sensor has a descriptor");
            }
            for (size_t bank = 0; reading.payload.size() < info.total_length; ++bank) {
                const auto count = std::min<size_t>(255, info.total_length - reading.payload.size());
                const auto data = owner_->read_range(static_cast<uint8_t>(0x32 + bank), 0, count, false, op.deadline());
                reading.payload.insert(reading.payload.end(), data.begin(), data.end());
            }
            // Check that selection did not change while bank reads were in flight.
            if (owner_->read_range(0x31, 0x6e, 1, false, op.deadline())[0] != index) {
                throw ProtocolError("L6 sensor identity changed during read");
            }
            reading.timestamp = owner_->runtime_->response_provenance().received_at;
            info.timestamp = reading.timestamp;
            if (!reading.payload.empty()) {
                (void)hand::protocol::hop::decode_tactile_fields(reading, true);
            }
            if (index == 1) {
                round.info = info;
            }
            round.timestamp = reading.timestamp;
            round.payload.insert(round.payload.end(), reading.payload.begin(), reading.payload.end());
            round.sensors.push_back(std::move(reading));
        }
        if (round.payload.empty()) {
            throw UnsupportedOperationError("L6 HOP reports no available fingertip sensors");
        }
        const auto provenance = owner_->runtime_->response_provenance();
        auto result = hand::protocol::hop::descriptor_force_sample(round, owner_->metadata(provenance, "force"), true);
        owner_->publish("force", {TelemetrySemantic::kForce, result}, provenance);
        return result;
    }
    std::optional<ForceSample> HopL6::ForceSensorManager::get_snapshot() const {
        return owner_->runtime_->snapshot_as<ForceSample>("force");
    }
    HandSnapshot HopL6::get_snapshot() const {
        HandSnapshot result;
        result.model = Model::kL6;
        result.side = capabilities_.side;
        result.captured_at = runtime::Clock::now();
        result.connection_generation = runtime_->runtime_health().device_generation;
        result.angles = angle.get_snapshot();
        result.configured_speeds = vector_snapshot<ConfiguredSpeedSample>(runtime_, "speeds");
        result.currents = vector_snapshot<CurrentSample>(runtime_, "currents");
        result.current_limits = vector_snapshot<CurrentLimitSample>(runtime_, "current_limits");
        result.temperatures = vector_snapshot<TemperatureSample>(runtime_, "temperatures");
        result.faults = fault.get_snapshot();
        result.force = force_sensor.get_snapshot();
        result.device_info = version.get_snapshot();
        const auto fresh = [&](auto &sample, TelemetrySemantic semantic) {
            if (!sample) {
                return;
            }
            auto &meta = sample->meta;
            const auto *channel = capabilities_.telemetry_channel(semantic);
            if (meta.collected_at) {
                meta.age = result.captured_at - *meta.collected_at;
            }
            meta.stale = !meta.valid || (meta.age && channel && channel->freshness_ms &&
                                         *meta.age > std::chrono::duration<double, std::milli>{*channel->freshness_ms});
            result.snapshot_sequence = std::max(result.snapshot_sequence, meta.sequence);
        };
        fresh(result.angles, TelemetrySemantic::kAngles);
        fresh(result.configured_speeds, TelemetrySemantic::kConfiguredSpeeds);
        fresh(result.currents, TelemetrySemantic::kCurrents);
        fresh(result.current_limits, TelemetrySemantic::kCurrentLimits);
        fresh(result.temperatures, TelemetrySemantic::kTemperatures);
        fresh(result.faults, TelemetrySemantic::kFaults);
        fresh(result.force, TelemetrySemantic::kForce);
        for (size_t i = 0; i < 6; ++i) {
            JointState state;
            state.index = i;
            state.name = kJointNames[i];
            if (result.angles) {
                state.angle_raw = result.angles->raw[i];
                state.angle_percent = result.angles->percent[i];
            }
            if (result.configured_speeds) {
                state.configured_speed_raw = result.configured_speeds->raw[i];
            }
            if (result.currents) {
                state.current_raw = result.currents->raw[i];
            }
            if (result.current_limits) {
                state.current_limit_raw = result.current_limits->raw[i];
            }
            if (result.temperatures) {
                state.temperature_raw = result.temperatures->raw[i];
            }
            if (result.faults) {
                state.fault_raw = result.faults->raw[i];
            }
            result.joints.push_back(state);
        }
        return result;
    }
    void HopL6::start_polling(const PollingPlan &plan) {
        auto effective = plan;
        if (effective.empty()) {
            effective = {{TelemetrySemantic::kAngles, std::chrono::milliseconds{100}}};
        }
        std::vector<runtime::DeviceRuntime::PollTask> tasks;
        for (const auto &channel : effective) {
            if (!capabilities_.supports(channel.semantic) || channel.interval.count() <= 0) {
                throw PollingPlanError("unsupported L6 HOP poll source or nonpositive interval");
            }
            const auto semantic = channel.semantic;
            const char *domain = semantic == TelemetrySemantic::kAngles             ? "angles"
                                 : semantic == TelemetrySemantic::kConfiguredSpeeds ? "speeds"
                                 : semantic == TelemetrySemantic::kCurrents         ? "currents"
                                 : semantic == TelemetrySemantic::kCurrentLimits    ? "current_limits"
                                 : semantic == TelemetrySemantic::kTemperatures     ? "temperatures"
                                 : semantic == TelemetrySemantic::kFaults           ? "faults"
                                                                                    : "force";
            if (std::any_of(tasks.begin(), tasks.end(), [domain](const auto &task) { return task.name == domain; })) {
                throw PollingPlanError("duplicate L6 HOP polling source");
            }
            runtime::DeviceRuntime::PollTask task;
            task.name = domain;
            task.period = channel.interval;
            task.isolate_failure = true;
            task.run = [this, semantic](runtime::CancellationToken) {
                switch (semantic) {
                case TelemetrySemantic::kAngles:
                    (void)angle.get_blocking();
                    break;
                case TelemetrySemantic::kConfiguredSpeeds:
                    (void)speed.get_blocking();
                    break;
                case TelemetrySemantic::kCurrents:
                    (void)current.get_blocking();
                    break;
                case TelemetrySemantic::kCurrentLimits:
                    (void)current_limit.get_blocking();
                    break;
                case TelemetrySemantic::kTemperatures:
                    (void)temperature.get_blocking();
                    break;
                case TelemetrySemantic::kFaults:
                    (void)fault.get_blocking();
                    break;
                case TelemetrySemantic::kForce:
                    (void)force_sensor.get_blocking();
                    break;
                default:
                    throw UnsupportedOperationError("unsupported L6 HOP polling source");
                }
            };
            tasks.push_back(std::move(task));
        }
        runtime_->start_polling(std::move(tasks));
    }
    void HopL6::stop_polling() { runtime_->stop_polling(); }
    std::shared_ptr<EventQueue<HandEvent>> HopL6::stream_handle(size_t max_size) {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
        if (runtime_->runtime_health().state != runtime::DeviceState::ready) {
            throw ClosedError("L6 HOP runtime is not ready");
        }
        const std::lock_guard<std::mutex> lock(event_mutex_);
        if (events_ && !events_->closed()) {
            throw LifecycleError("L6 HOP stream is already active");
        }
        if (events_) {
            retired_streams_.push_back(events_);
        }
        events_ = std::make_shared<EventQueue<HandEvent>>(max_size);
        return events_;
    }
    void HopL6::stop_stream() {
        const std::lock_guard<std::mutex> lock(event_mutex_);
        if (events_) {
            events_->close();
        }
    }
    void HopL6::stop() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
        stop_stream();
        std::exception_ptr error;
        try {
            runtime_->close();
        } catch (...) {
            error = std::current_exception();
        }
        calls_.drain();
        try {
            if (owns_session_) {
                session_->close();
            }
        } catch (...) {
            if (!error) {
                error = std::current_exception();
            }
        }
        if (error) {
            std::rethrow_exception(error);
        }
    }
    void HopL6::recover() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
        stop_stream();
        runtime_->cancel_all();
        calls_.drain();
        runtime_->recover();
    }
}
