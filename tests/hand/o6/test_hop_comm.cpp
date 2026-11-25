#include "hand/support/optional_value.hpp"
#include "hop_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <future>
#include <initializer_list>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/api/device.hpp>
#include <linkerbot/hand/protocol/tactile.hpp>
#include <optional>
#include <thread>

using namespace linkerbot::hand;
using namespace std::chrono_literals;
using o6_hop_test::config;
using o6_hop_test::Firmware;

TEST_CASE("O6 HOP explicit capabilities preserve classic default", "[comm][o6-hop]") {
    auto cfg = config(0x123, true);
    const auto fd_profile = declared_capabilities(cfg);
    REQUIRE(fd_profile.validate());
    REQUIRE(fd_profile.transport.kind == TransportKind::kCanFd);
    REQUIRE(fd_profile.transport.mtu == 64);
    REQUIRE(fd_profile.transport.request_id == 0x123);
    REQUIRE(fd_profile.transport.response_id == 0x523);
    REQUIRE(fd_profile.joints[0].raw_angle_range.maximum == 255);
    REQUIRE(fd_profile.supports(CommandSemantic::kAccelerations));
    REQUIRE(fd_profile.supports(CommandSemantic::kStallDetectionTimes));
    REQUIRE(fd_profile.supports(CommandSemantic::kStallThresholds));
    REQUIRE(fd_profile.supports(CommandSemantic::kStallHoldCurrents));
    REQUIRE(fd_profile.supports(TelemetrySemantic::kConfiguredAccelerations));
    REQUIRE(fd_profile.supports(TelemetrySemantic::kStallDetectionTimes));
    REQUIRE(fd_profile.supports(TelemetrySemantic::kStallThresholds));
    REQUIRE(fd_profile.supports(TelemetrySemantic::kStallHoldCurrents));
    REQUIRE(fd_profile.force_field("fn").supported == std::nullopt);
    REQUIRE_FALSE(fd_profile.supports(DeviceOperation::kClearFaults));
    REQUIRE_FALSE(fd_profile.supports(DeviceOperation::kEnable));

    const auto can_profile = declared_capabilities(config(0x045, false));
    REQUIRE(can_profile.validate());
    REQUIRE(can_profile.transport.kind == TransportKind::kCan);
    REQUIRE(can_profile.transport.mtu == 8);
    REQUIRE(can_profile.transport.request_id == 0x045);
    REQUIRE(can_profile.transport.response_id == 0x445);

    REQUIRE(declared_capabilities(Model::kO6, HandSide::kRight).transport.kind == TransportKind::kCan);
    REQUIRE(declared_capabilities(Model::kO6, HandSide::kRight).transport.request_id == 0x27);

    cfg = config();
    cfg.model = Model::kL6;
    REQUIRE_THROWS_AS(HandDevice(cfg), ValidationError);
    cfg = config();
    cfg.l6_protocol = L6Protocol::kHopV004;
    REQUIRE_THROWS_AS(HandDevice(cfg), ValidationError);
    cfg = config();
    cfg.runtime_behavior = RuntimeBehavior::kPython;
    REQUIRE_THROWS_AS(HandDevice(cfg), ValidationError);
    cfg = config();
    cfg.address = 0x3ff;
    REQUIRE_THROWS_AS(HandDevice(cfg), ValidationError);
    cfg = config();
    cfg.angle_mapping_path = "old.json";
    REQUIRE_THROWS_AS(HandDevice(cfg), ValidationError);
    cfg = config();
    cfg.host_address = 1;
    REQUIRE_THROWS_AS(HandDevice(cfg), ValidationError);
    cfg = config();
    cfg.side = HandSide::kUnknown;
    REQUIRE_THROWS_AS(HandDevice(cfg), ValidationError);
}

TEST_CASE("O6 HOP unified open reads and writes all O6 channels over CAN FD", "[comm][o6-hop]") {
    auto firmware = std::make_shared<Firmware>(true);
    auto cfg = config(0x123, true);
    HandDevice device(cfg, [firmware] { return firmware->transport(0x123); });
    REQUIRE(device.get_capabilities().transport.kind == TransportKind::kCanFd);
    device.open();

    const auto identity = test_support::require_value(device.get_identity());
    REQUIRE(identity.model == Model::kO6);
    REQUIRE(identity.side == HandSide::kRight);
    REQUIRE(identity.protocol_name == "HOP");
    REQUIRE(identity.protocol_version == "1.0.0");
    REQUIRE(identity.firmware_version == "1.0.3");
    REQUIRE(identity.serial_number == "632");

    REQUIRE(device.get_angles().raw == std::vector<double>{10, 51, 255, 0, 128, 200});
    REQUIRE(device.get_angles().percent[2] == 100.0);
    REQUIRE(device.get_faults().statuses[0].known_bits == 0x80);
    REQUIRE(device.get_faults().statuses[0].reserved_bits == 0x30);
    REQUIRE(device.get_currents().raw[0] == 1);
    REQUIRE(device.get_current_limits().raw[5] == 6);
    REQUIRE(device.get_configured_speeds().meta.origin == TelemetryValueOrigin::kDeviceConfigured);
    REQUIRE(device.get_configured_accelerations().meta.origin == TelemetryValueOrigin::kDeviceConfigured);
    REQUIRE(device.get_temperatures().raw[0] == 1);
    REQUIRE(device.get_stall_detection_times().raw == std::vector<double>{1, 2, 3, 4, 5, 6});
    REQUIRE(device.get_stall_thresholds().raw == std::vector<double>{1, 2, 3, 4, 5, 6});
    REQUIRE(device.get_stall_hold_currents().raw == std::vector<double>{1, 2, 3, 4, 5, 6});
    REQUIRE_THROWS_AS(device.clear_faults(), UnsupportedOperationError);
    REQUIRE_THROWS_AS(device.enable(), UnsupportedOperationError);

    const auto before_sequence = device.get_angles().meta.sequence;
    auto set_pos = device.set_angles({255, 10, 20, 128, 1, 0});
    REQUIRE(set_pos.wait_for(1s));
    REQUIRE(set_pos.stage() == CommandStage::kAcknowledged);
    REQUIRE(set_pos.sent_frames() == 2);
    REQUIRE(test_support::require_value(device.get_snapshot().angles).meta.sequence > before_sequence);
    REQUIRE(device.get_angles().raw == std::vector<double>{255, 10, 20, 128, 1, 0});

    auto percent = device.set_percent_angles({0, 100, 50, 20, 80, 0});
    REQUIRE(percent.wait_for(1s));
    REQUIRE(percent.stage() == CommandStage::kAcknowledged);
    REQUIRE(device.get_angles().raw == std::vector<double>{0, 255, 128, 51, 204, 0});

    auto speed = device.set_speeds({10, 20, 30, 40, 50, 255});
    REQUIRE(speed.wait_for(1s));
    REQUIRE(speed.stage() == CommandStage::kAcknowledged);
    REQUIRE(device.get_configured_speeds().raw == std::vector<double>{10, 20, 30, 40, 50, 255});

    auto accel = device.set_accelerations({5, 15, 25, 35, 45, 255});
    REQUIRE(accel.wait_for(1s));
    REQUIRE(accel.stage() == CommandStage::kAcknowledged);
    REQUIRE(device.get_configured_accelerations().raw == std::vector<double>{5, 15, 25, 35, 45, 255});

    auto current = device.set_current_limits({255, 2, 3, 4, 5, 6});
    REQUIRE(current.wait_for(1s));
    REQUIRE(current.stage() == CommandStage::kAcknowledged);
    REQUIRE(device.get_current_limits().raw[0] == 255);

    auto stall_time = device.set_stall_detection_times({11, 12, 13, 14, 15, 16});
    REQUIRE(stall_time.wait_for(1s));
    REQUIRE(stall_time.stage() == CommandStage::kAcknowledged);
    REQUIRE(device.get_stall_detection_times().raw == std::vector<double>{11, 12, 13, 14, 15, 16});

    auto stall_threshold = device.set_stall_thresholds({21, 22, 23, 24, 25, 26});
    REQUIRE(stall_threshold.wait_for(1s));
    REQUIRE(stall_threshold.stage() == CommandStage::kAcknowledged);
    REQUIRE(device.get_stall_thresholds().raw == std::vector<double>{21, 22, 23, 24, 25, 26});

    auto stall_hold = device.set_stall_hold_currents({31, 32, 33, 34, 35, 36});
    REQUIRE(stall_hold.wait_for(1s));
    REQUIRE(stall_hold.stage() == CommandStage::kAcknowledged);
    REQUIRE(device.get_stall_hold_currents().raw == std::vector<double>{31, 32, 33, 34, 35, 36});

    const auto snapshot = device.get_snapshot();
    REQUIRE(snapshot.joints.size() == 6);
    REQUIRE(snapshot.joints[0].configured_acceleration_raw == 5);
    REQUIRE(snapshot.joints[0].stall_detection_time_raw == 11);
    REQUIRE(snapshot.joints[0].stall_threshold_raw == 21);
    REQUIRE(snapshot.joints[0].stall_hold_current_raw == 31);

    auto saved = device.save_parameters();
    REQUIRE(saved.wait_for(1s));
    REQUIRE(saved.stage() == CommandStage::kAcknowledged);

    device.close();
    REQUIRE(device.get_capabilities().transport.kind == TransportKind::kCanFd);
}

TEST_CASE("O6 HOP supports Classic CAN 2.0 with 5-byte chunking through HandDevice", "[comm][o6-hop]") {
    auto firmware = std::make_shared<Firmware>(false);
    Firmware::put(firmware->info, 0, "O6", 16);
    Firmware::put(firmware->info, 0x39, "LHO6XXXXXXXXXXXX", 32);
    auto transport = firmware->transport(0x021);
    auto *fake = transport.get();
    auto session = linkerbot::transport::Session::own(std::move(transport));

    auto cfg = config(0x021, false);
    cfg.identity_timeout = 2s;
    cfg.transaction_timeout = 1s;
    cfg.command_timeout = 1s;
    HandDevice device(cfg, session);
    REQUIRE(device.get_capabilities().transport.kind == TransportKind::kCan);
    device.open();

    const auto identity = test_support::require_value(device.get_identity());
    REQUIRE(identity.model == Model::kO6);
    REQUIRE(identity.device_uid == "LHO6XXXXXXXXXXXX");
    REQUIRE(identity.product_code == "O6");

    auto set_pos = device.set_angles({100, 110, 120, 130, 140, 150});
    REQUIRE(set_pos.wait_for(1s));
    REQUIRE(set_pos.stage() == CommandStage::kAcknowledged);
    REQUIRE(device.get_angles().raw == std::vector<double>{100, 110, 120, 130, 140, 150});

    const auto force = device.get_force(5s);
    REQUIRE(force.meta.valid);
    REQUIRE(force.sensors.size() == 5);
    REQUIRE(test_support::require_value(force.field("fns")->raw[0]).at(0) == 42);
    REQUIRE(force.field("fn")->raw[0] == std::vector<double>{1, 2, 3, 4});

    const auto sent = fake->sent();
    REQUIRE_FALSE(sent.empty());
    for (const auto &req : sent) {
        REQUIRE_FALSE(req.is_fd);
        REQUIRE_FALSE(req.is_extended);
        REQUIRE(req.dlc <= 8);
        REQUIRE(req.data[2] <= 5);
    }

    device.close();
    session->close();
}

TEST_CASE("O6 HOP angle command waits for both mounted replies and isolates partial failure", "[comm][o6-hop]") {
    auto firmware = std::make_shared<Firmware>(true);
    auto transport = firmware->transport();
    auto *fake = transport.get();
    auto session = linkerbot::transport::Session::own(std::move(transport));
    HandDevice device(config(), session);
    device.open();
    const auto before = device.get_snapshot().angles;
    {
        std::lock_guard<std::mutex> lock(firmware->mutex);
        firmware->hold_tail = true;
    }
    auto handle = device.set_angles({5, 6, 7, 8, 9, 10});
    REQUIRE_FALSE(handle.wait_for(20ms));
    REQUIRE(handle.stage() == CommandStage::kDispatched);
    REQUIRE(test_support::require_value(device.get_snapshot().angles).raw == test_support::require_value(before).raw);
    std::optional<linkerbot::can::Message> held;
    {
        std::lock_guard<std::mutex> lock(firmware->mutex);
        held = firmware->held;
    }
    REQUIRE(held);
    fake->inject(test_support::require_value(held));
    REQUIRE(handle.wait_for(1s));
    REQUIRE(handle.stage() == CommandStage::kAcknowledged);
    REQUIRE(test_support::require_value(device.get_snapshot().angles).raw == std::vector<double>{5, 6, 7, 8, 9, 10});

    {
        std::lock_guard<std::mutex> lock(firmware->mutex);
        firmware->hold_tail = false;
        firmware->short_tail = true;
    }
    auto failed = device.set_angles({1, 2, 3, 4, 5, 6});
    REQUIRE(failed.wait_for(1s));
    REQUIRE(failed.stage() == CommandStage::kFailed);
    REQUIRE(failed.error());
    REQUIRE(test_support::require_value(device.get_snapshot().angles).raw == std::vector<double>{5, 6, 7, 8, 9, 10});

    device.close();
    session->close();
}

TEST_CASE("O6 HOP identity rejects wrong model side and protocol", "[comm][o6-hop]") {
    for (int scenario = 0; scenario < 3; ++scenario) {
        auto firmware = std::make_shared<Firmware>(true);
        if (scenario == 0) {
            Firmware::put(firmware->info, 0, "L6", 16);
        }
        if (scenario == 1) {
            Firmware::put(firmware->info, 0xf1, "LEFT", 8);
        }
        if (scenario == 2) {
            Firmware::put(firmware->info, 0x59, "OTHER", 8);
        }
        HandDevice device(config(), [firmware] { return firmware->transport(); });
        REQUIRE_THROWS(device.open());
        REQUIRE_FALSE(device.is_open());
        device.close();
    }
}

TEST_CASE("O6 HOP unified dynamic force capabilities polling stream and recover", "[comm][o6-hop]") {
    auto firmware = std::make_shared<Firmware>(true);
    HandDevice device(config(), [firmware] { return firmware->transport(); });
    device.open();

    const auto force = device.get_force({"fn", "fns", "fts", "fds"}, 2s);
    REQUIRE(force.field("fn"));
    REQUIRE(force.field("fns"));
    REQUIRE(force.field("fts"));
    REQUIRE(device.get_capabilities().force_field("fns").supported == true);

    auto handle = device.set_angles({7, 8, 9, 10, 11, 12});
    REQUIRE(handle.wait_for(1s));
    REQUIRE(handle.stage() == CommandStage::kAcknowledged);

    auto stream = device.stream_handle();
    device.start_polling({{TelemetrySemantic::kAngles, 10ms},
                          {TelemetrySemantic::kConfiguredAccelerations, 20ms},
                          {TelemetrySemantic::kStallThresholds, 20ms}});
    const auto previous = device.get_snapshot();
    const auto next = device.wait_for_snapshot(previous, 500ms);
    REQUIRE(test_support::require_value(next.angles).meta.valid);

    device.recover();
    REQUIRE(device.is_open());
    REQUIRE(test_support::require_value(device.get_identity()).device_uid.has_value());
    REQUIRE(device.get_angles().raw == std::vector<double>{7, 8, 9, 10, 11, 12});
    {
        std::lock_guard<std::mutex> lock(firmware->mutex);
        REQUIRE(firmware->position_writes == 2);
    }

    device.stop_polling();
    device.stop_stream();
    device.close();
}

TEST_CASE("O6 HOP shares a bus without routing one hand to another", "[comm][o6-hop]") {
    auto right = std::make_shared<Firmware>(true);
    auto left = std::make_shared<Firmware>(true);
    Firmware::put(left->info, 0xf1, "LEFT", 8);
    Firmware::put(left->info, 0x39, "LHO6-03.3-633-L-B-1-D", 32);
    auto transport = right->transport(1);
    auto *fake = transport.get();
    fake->on_request(2, [left](const auto &request) { return left->handle(request); });
    auto session = linkerbot::transport::Session::own(std::move(transport));

    auto cfg_right = config(1, true);
    auto cfg_left = config(2, true);
    cfg_left.side = HandSide::kLeft;
    HandDevice hand_right(cfg_right, session);
    HandDevice hand_left(cfg_left, session);
    hand_right.open();
    hand_left.open();

    REQUIRE(hand_right.set_angles({1, 2, 3, 4, 5, 6}).wait_for(1s));
    REQUIRE(hand_left.set_angles({101, 102, 103, 104, 105, 106}).wait_for(1s));
    REQUIRE(hand_right.get_angles().raw == std::vector<double>{1, 2, 3, 4, 5, 6});
    REQUIRE(hand_left.get_angles().raw == std::vector<double>{101, 102, 103, 104, 105, 106});

    hand_right.close();
    REQUIRE(fake->close_calls() == 0);
    REQUIRE(hand_left.get_angles().raw[0] == 101);
    hand_left.close();
    session->close();
}

namespace {
    using linkerbot::can::FakeTransport;
    using linkerbot::can::Message;

    [[nodiscard]] size_t count_prefix(const std::vector<Message> &sent, std::initializer_list<uint8_t> prefix,
                                      std::optional<bool> fd = std::nullopt) {
        size_t count = 0;
        for (const auto &message : sent) {
            if (message.dlc < prefix.size()) {
                continue;
            }
            bool match = true;
            size_t index = 0;
            for (const auto byte : prefix) {
                if (message.data[index++] != byte) {
                    match = false;
                    break;
                }
            }
            if (match && (!fd || message.is_fd == *fd)) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] size_t count_config_writes(const std::vector<Message> &sent) {
        size_t count = 0;
        for (const auto &message : sent) {
            if (message.dlc >= 4 && message.data[0] == 0x35) {
                ++count;
            }
        }
        return count;
    }

    struct OpenedHop {
        std::shared_ptr<Firmware> firmware;
        FakeTransport *transport = nullptr;
        std::unique_ptr<HandDevice> device;

        OpenedHop(bool can_fd, uint32_t id) : firmware(std::make_shared<Firmware>(can_fd)) {
            device = std::make_unique<HandDevice>(config(id, can_fd), [this, id] {
                auto next = firmware->transport(id);
                transport = next.get();
                return next;
            });
            device->open();
            transport->clear_sent();
        }
    };

    void install_classic_o6(FakeTransport *transport) {
        const std::string code = "LHO6-1-123-L";
        transport->on_request(0x28, [code](const Message &request) {
            if (request.data[0] == 0xC0) {
                std::vector<Message> replies;
                for (int chunk = 0; chunk < 4; ++chunk) {
                    Message reply{};
                    reply.id = request.id;
                    reply.dlc = 8;
                    reply.data[0] = 0xC0;
                    reply.data[1] = static_cast<uint8_t>(chunk);
                    for (size_t index = 0; index < 6; ++index) {
                        const auto offset = static_cast<size_t>(chunk) * 6 + index;
                        reply.data[index + 2] = offset < code.size() ? static_cast<uint8_t>(code[offset]) : uint8_t{0};
                    }
                    replies.push_back(reply);
                }
                return replies;
            }
            if (request.data[0] == 0xC1 || request.data[0] == 0xC2 || request.data[0] == 0xC4) {
                Message reply{};
                reply.id = request.id;
                reply.dlc = 4;
                reply.data[0] = request.data[0];
                reply.data[1] = 1;
                reply.data[2] = 2;
                reply.data[3] = 3;
                return std::vector<Message>{reply};
            }
            Message reply{};
            reply.id = request.id;
            reply.dlc = 7;
            reply.data[0] = request.data[0];
            for (size_t index = 1; index < 7; ++index) {
                reply.data[index] = request.dlc > index ? request.data[index] : uint8_t{64};
            }
            return std::vector<Message>{reply};
        });
    }
} // namespace

TEST_CASE("O6 HOP reads and switches CAN interface mode", "[comm][o6-hop]") {
    const auto hop = declared_capabilities(config(0x123, true));
    REQUIRE(hop.operations.read_can_configuration);
    REQUIRE(hop.operations.write_can_configuration);
    REQUIRE(declared_capabilities(config(0x045, false)).operations.write_can_configuration);
    REQUIRE_FALSE(declared_capabilities(Model::kO6, HandSide::kRight).operations.read_can_configuration);
    REQUIRE_FALSE(declared_capabilities(Model::kO6, HandSide::kRight).operations.write_can_configuration);

    SECTION("CAN FD read") {
        OpenedHop opened(true, 0x123);
        const auto configuration = opened.device->get_can_configuration();
        REQUIRE(configuration.request_id == 0x123);
        REQUIRE(configuration.response_id() == 0x523);
        REQUIRE(configuration.interface_mode == HopCanInterfaceMode::kCanFd);
        REQUIRE_FALSE(configuration.brs);
        REQUIRE(configuration.nominal_bitrate == 1000000);
        REQUIRE(configuration.data_bitrate == 5000000);
        const auto sent = opened.transport->sent();
        REQUIRE(count_prefix(sent, {0xB5, 0x00, 0x02}, true) == 1);
        REQUIRE(count_prefix(sent, {0xB5, 0x06, 0x04}, true) == 1);
    }
    SECTION("CAN 2.0 read") {
        OpenedHop opened(false, 0x045);
        const auto configuration = opened.device->get_can_configuration();
        REQUIRE(configuration.request_id == 0x045);
        REQUIRE(configuration.interface_mode == HopCanInterfaceMode::kCan20);
        const auto sent = opened.transport->sent();
        REQUIRE(count_prefix(sent, {0xB5, 0x00, 0x02}, false) == 1);
        REQUIRE(count_prefix(sent, {0xB5, 0x06, 0x04}, false) == 1);
        for (const auto &message : sent) {
            REQUIRE_FALSE(message.is_fd);
            REQUIRE(message.dlc <= 8);
        }
    }
    SECTION("mode write stays on CAN FD until reboot and accepts flash completion 0x01") {
        OpenedHop opened(true, 0x123);
        const auto result = opened.device->set_can_interface_mode(HopCanInterfaceMode::kCan20, true);
        REQUIRE(result.write_sent);
        REQUIRE(result.saved_to_flash);
        REQUIRE(result.active_wire_is_fd);
        REQUIRE_FALSE(result.active_format_changed);
        REQUIRE(result.before.interface_mode == HopCanInterfaceMode::kCanFd);
        REQUIRE(result.after.interface_mode == HopCanInterfaceMode::kCan20);
        REQUIRE(result.after.brs == result.before.brs);
        REQUIRE(result.after.nominal_bitrate == result.before.nominal_bitrate);
        REQUIRE(result.after.data_bitrate == result.before.data_bitrate);
        const auto sent = opened.transport->sent();
        REQUIRE(count_prefix(sent, {0x35, 0x06, 0x01, 0x01}, true) == 1);
        REQUIRE(count_prefix(sent, {0x35, 0x1E, 0x01, 0x01}, true) == 1);
    }
    SECTION("firmware switches to classic CAN immediately and flash completion may be 0x00") {
        OpenedHop opened(true, 0x123);
        opened.firmware->switch_wire_immediately = true;
        opened.firmware->override_save_ack = true;
        opened.firmware->save_ack = 0x00;
        const auto result = opened.device->set_can_interface_mode(HopCanInterfaceMode::kCan20, true);
        REQUIRE(result.write_sent);
        REQUIRE(result.saved_to_flash);
        REQUIRE_FALSE(result.active_wire_is_fd);
        REQUIRE(result.active_format_changed);
        REQUIRE(result.after.interface_mode == HopCanInterfaceMode::kCan20);
        const auto sent = opened.transport->sent();
        REQUIRE(count_prefix(sent, {0x35, 0x06, 0x01, 0x01}, true) == 1);
        REQUIRE(count_prefix(sent, {0x35, 0x1E, 0x01, 0x01}, false) == 1);
    }
    SECTION("classic host writes CAN FD mode once and keeps the current wire") {
        OpenedHop opened(false, 0x045);
        const auto result = opened.device->set_can_interface_mode(HopCanInterfaceMode::kCanFd, true);
        REQUIRE(result.write_sent);
        REQUIRE(result.saved_to_flash);
        REQUIRE_FALSE(result.active_wire_is_fd);
        REQUIRE_FALSE(result.active_format_changed);
        REQUIRE(result.after.interface_mode == HopCanInterfaceMode::kCanFd);
        const auto sent = opened.transport->sent();
        REQUIRE(count_prefix(sent, {0x35, 0x06, 0x01, 0x02}, false) == 1);
        REQUIRE(count_prefix(sent, {0x35, 0x1E, 0x01, 0x01}, false) == 1);
    }
    SECTION("an already selected mode is not written again") {
        OpenedHop opened(true, 0x123);
        const auto result = opened.device->set_can_interface_mode(HopCanInterfaceMode::kCanFd, true);
        REQUIRE_FALSE(result.write_sent);
        REQUIRE(result.saved_to_flash);
        REQUIRE_FALSE(result.active_format_changed);
        const auto sent = opened.transport->sent();
        REQUIRE(count_prefix(sent, {0x35, 0x06, 0x01}) == 0);
        REQUIRE(count_prefix(sent, {0x35, 0x1E, 0x01, 0x01}, true) == 1);
    }
    SECTION("classic CAN is rejected while BRS is enabled") {
        OpenedHop opened(true, 0x123);
        opened.firmware->registers[o6::hop::kMiConfig][7] = 1;
        REQUIRE_THROWS_AS(opened.device->set_can_interface_mode(HopCanInterfaceMode::kCan20, true), ValidationError);
        REQUIRE(count_config_writes(opened.transport->sent()) == 0);
    }
    SECTION("a missed mode ACK is verified once on the new wire format") {
        OpenedHop opened(true, 0x123);
        opened.firmware->switch_wire_immediately = true;
        opened.firmware->drop_mode_write_reply = true;
        const auto result = opened.device->set_can_interface_mode(HopCanInterfaceMode::kCan20, false, 100ms);
        REQUIRE(result.write_sent);
        REQUIRE_FALSE(result.saved_to_flash);
        REQUIRE(result.active_format_changed);
        REQUIRE_FALSE(result.active_wire_is_fd);
        REQUIRE(count_prefix(opened.transport->sent(), {0x35, 0x06, 0x01, 0x01}) == 1);
    }
}

TEST_CASE("classic O6 rejects CAN configuration before any frame", "[comm][o6-hop]") {
    FakeTransport *transport = nullptr;
    HandDeviceConfig cfg;
    cfg.model = Model::kO6;
    cfg.side = HandSide::kLeft;
    cfg.interface_name = "fake0";
    HandDevice device(cfg, [&] {
        auto next = std::make_unique<FakeTransport>();
        transport = next.get();
        install_classic_o6(transport);
        return next;
    });
    device.open();
    transport->clear_sent();
    REQUIRE_THROWS_AS(device.get_can_configuration(), UnsupportedOperationError);
    REQUIRE(transport->sent().empty());
    REQUIRE_THROWS_AS(device.set_can_interface_mode(HopCanInterfaceMode::kCan20, true), UnsupportedOperationError);
    REQUIRE(transport->sent().empty());
    device.close();
}
