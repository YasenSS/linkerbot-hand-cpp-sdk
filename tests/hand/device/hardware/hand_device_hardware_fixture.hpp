#pragma once

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hand_device_connection.hpp"
#include <linkerbot/hand/api/device.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/transport/session.hpp>

namespace linkerbot::hand::hardware_test {
    inline std::optional<Model> parse_model(std::string value) {
        for (auto &character : value) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        if (value == "l6") {
            return Model::kL6;
        }
        if (value == "o6") {
            return Model::kO6;
        }
        if (value == "l20lite" || value == "l20-lite") {
            return Model::kL20Lite;
        }
        if (value == "l20") {
            return Model::kL20;
        }
        if (value == "l30") {
            return Model::kL30;
        }
        if (value == "o30") {
            return Model::kO30;
        }
        return std::nullopt;
    }

    inline HandDeviceConfig config_from_environment() {
        const auto model_name = env("HAND_DEVICE_MODEL");
        if (!model_name) {
            SKIP("set HAND_DEVICE_MODEL to run hardware tests");
        }
        const auto model = parse_model(*model_name);
        if (!model) {
            throw std::invalid_argument("HAND_DEVICE_MODEL must be l6, o6, l20lite, l20, l30, or o30");
        }

        HandDeviceConfig config;
        config.model = *model;
        config.interface_name = env("CAN_INTERFACE").value_or("can0");
        if (env("HAND_DEVICE_BACKEND").value_or("socketcan") != "socketcan") {
            config.interface_name = env("HAND_DEVICE_BACKEND").value() + ":" + env("HAND_DEVICE_CHANNEL").value_or("");
        }
        const auto side = env("HAND_DEVICE_SIDE");
        if (side && (*side == "left" || *side == "LEFT")) {
            config.side = HandSide::kLeft;
        } else if (side && (*side == "right" || *side == "RIGHT")) {
            config.side = HandSide::kRight;
        } else if (side) {
            throw std::invalid_argument("HAND_DEVICE_SIDE must be left or right");
        } else if (*model != Model::kL30 && *model != Model::kO30) {
            config.side = HandSide::kLeft;
        }
        config.address = parse_u32(env("HAND_DEVICE_ADDRESS"));
        config.host_address = parse_u32(env("HAND_DEVICE_HOST_ADDRESS"));
        if (env("HAND_DEVICE_BACKEND").value_or("socketcan") != "socketcan") {
            if (!config.address) {
                throw std::invalid_argument("Adapter acceptance requires explicit HAND_DEVICE_ADDRESS");
            }
            if (!side && *model != Model::kL30 && *model != Model::kO30) {
                throw std::invalid_argument("Classic adapter acceptance requires explicit HAND_DEVICE_SIDE");
            }
        }
        return config;
    }

    inline bool destructive_enabled() {
        const auto value = env("HAND_DEVICE_ENABLE_DESTRUCTIVE_TESTS");
        return value && (*value == "1" || *value == "true" || *value == "TRUE");
    }

    class DeviceFixture {
    public:
        DeviceFixture() : DeviceFixture(config_from_environment()) {}
        explicit DeviceFixture(HandDeviceConfig config) : DeviceFixture(config, hardware_factory(config)) {}
        DeviceFixture(HandDeviceConfig config, HandDevice::TransportFactory factory) : config_(std::move(config)) {
            // O30 setpoint queries share this physical channel with HandDevice.
            // Opening a second PCAN/MiniCANFD connection would conflict.
            if (config_.model == Model::kO30) {
                session_ = transport::Session::create(factory);
                device_ = std::make_unique<HandDevice>(config_, session_);
            } else {
                device_ = std::make_unique<HandDevice>(config_, factory);
            }
        }
        ~DeviceFixture() {
            try {
                device_->stop_stream();
            } catch (...) {
            }
            try {
                device_->stop_polling();
            } catch (...) {
            }
            try {
                device_->close();
            } catch (...) {
            }
            try {
                if (session_) {
                    session_->close();
                }
            } catch (...) {
            }
        }

        HandDevice &device() { return *device_; }
        const HandDeviceConfig &config() const { return config_; }
        std::shared_ptr<transport::Session> session() const { return session_; }
        o30::O30 *setpoint_reader() {
            if (config_.model != Model::kO30) {
                return nullptr;
            }
            if (!setpoint_reader_) {
                setpoint_reader_ =
                    std::make_unique<o30::O30>(session_, config_.address.value_or(o30::protocol::kDefaultRequestId));
            }
            return setpoint_reader_.get();
        }

    private:
        HandDeviceConfig config_;
        std::shared_ptr<transport::Session> session_;
        std::unique_ptr<o30::O30> setpoint_reader_;
        std::unique_ptr<HandDevice> device_;
    };

    inline void require_done(CommandHandle handle) {
        REQUIRE(handle.wait_for(std::chrono::seconds{2}));
        REQUIRE(handle.error() == nullptr);
        REQUIRE(handle.done());
        REQUIRE(handle.sent_frames() > 0);
        REQUIRE(handle.stage() == (handle.ack_expectation() == AckExpectation::kRequired ? CommandStage::kAcknowledged
                                                                                         : CommandStage::kDispatched));
        const auto history = handle.history();
        REQUIRE_FALSE(history.empty());
        REQUIRE(history.front().stage == CommandStage::kAccepted);
        REQUIRE(history.back().stage == handle.stage());
        for (size_t i = 1; i < history.size(); ++i) {
            REQUIRE(history[i].timestamp >= history[i - 1].timestamp);
            REQUIRE(history[i].sent_frames >= history[i - 1].sent_frames);
        }
    }

    inline void require_raw_sample(const RawVectorSample &sample, size_t joint_count) {
        REQUIRE(sample.meta.valid);
        REQUIRE(sample.raw.size() == joint_count);
        for (const auto value : sample.raw) {
            REQUIRE(std::isfinite(value));
        }
    }

    inline void require_meta(const SampleMeta &meta, uint64_t generation) {
        REQUIRE(meta.valid);
        REQUIRE_FALSE(meta.stale);
        REQUIRE(meta.collected_at.has_value());
        REQUIRE(meta.age.has_value());
        REQUIRE(*meta.age >= std::chrono::steady_clock::duration::zero());
        REQUIRE(meta.connection_generation == generation);
        REQUIRE(meta.origin != TelemetryValueOrigin::kNone);
        REQUIRE(meta.origin != TelemetryValueOrigin::kUnknown);
    }

    inline void require_identity_equal(const DeviceIdentity &actual, const DeviceIdentity &expected) {
        REQUIRE(actual.model == expected.model);
        REQUIRE(actual.side == expected.side);
        REQUIRE(actual.address == expected.address);
        REQUIRE(actual.product_code == expected.product_code);
        REQUIRE(actual.serial_number == expected.serial_number);
        REQUIRE(actual.device_uid == expected.device_uid);
        REQUIRE(actual.firmware_version == expected.firmware_version);
        REQUIRE(actual.hardware_version == expected.hardware_version);
        REQUIRE(actual.mechanical_version == expected.mechanical_version);
        REQUIRE(actual.protocol_name == expected.protocol_name);
        REQUIRE(actual.protocol_version == expected.protocol_version);
    }
}
