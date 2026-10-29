#include "hand/o30/comm_helpers.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <linkerbot/hand/errors.hpp>

using namespace linkerbot::hand::o30;
using namespace std::chrono_literals;
using linkerbot::can::Message;

TEST_CASE("comm: O30i engineering service rejects every truncated field", "[comm][o30][alignment][service]") {
    const auto si = GENERATE(0x00, 0x1E, 0x23, 0x25, 0x3D, 0x45, 0x49, 0x4A, 0x4B, 0x5B, 0x6B, 0x7B, 0xAB, 0xCD);
    auto h = o30_comm::make_hand();
    h.transport->on_request(1, [hop = h.hop, si](const Message &req) {
        if (req.data[0] != 0x42 || req.data[1] != si) {
            return hop->reply(req);
        }
        Message truncated{};
        truncated.id = 0x401;
        truncated.is_fd = true;
        truncated.data[0] = 0x42;
        truncated.data[1] = static_cast<uint8_t>(si);
        truncated.data[2] = static_cast<uint8_t>(req.data[2] - 1);
        truncated.dlc = protocol::canfd_wire_bytes(3 + truncated.data[2]);
        return std::vector<Message>{truncated};
    });
    REQUIRE_THROWS_AS(h.hand->service.get_status(100ms), protocol::ProtocolError);
}

TEST_CASE("comm: O30i engineering fields preserve lengths endian and offsets", "[comm][o30][alignment][service]") {
    auto h = o30_comm::make_hand();
    auto &object = h.hop->objects.at({0x42, false});
    object[0x23] = 0x34;
    object[0x24] = 0x12;
    object[0x45] = 0x78;
    object[0x46] = 0x56;
    object[0x47] = 0x34;
    object[0x48] = 0x12;
    object[0x49] = 1;
    object[0x4A] = 2;
    object[0xCD] = 3;
    h.hop->put_ascii(0x42, 0x25, 24, "2026-08-12");
    h.hop->put_ascii(0x42, 0x4B, 16, "123");
    h.hop->put_ascii(0x42, 0x5B, 16, "456");
    h.hop->put_ascii(0x42, 0x6B, 16, "789");
    h.hop->put_ascii(0x42, 0xAB, 32, "DEVICE-O30i");
    const auto status = h.hand->service.get_status();
    REQUIRE(status.calibration_count == 0x1234);
    REQUIRE(status.calibration_date == "2026-08-12");
    REQUIRE(status.heartbeat == 0x12345678);
    REQUIRE(status.power_on_time == "123");
    REQUIRE(status.total_runtime == "456");
    REQUIRE(status.motor_runtime == "789");
    REQUIRE(status.device_uid == "DEVICE-O30i");
    REQUIRE(status.protocol_interface == 3);
    REQUIRE(status.task_frequency.size() == 48);
    const std::vector<std::vector<uint8_t>> expected{
        {0x42, 0x00, 8},  {0x42, 0x1E, 5},  {0x42, 0x23, 2},  {0x42, 0x25, 24}, {0x42, 0x3D, 8},
        {0x42, 0x45, 4},  {0x42, 0x49, 1},  {0x42, 0x4A, 1},  {0x42, 0x4B, 16}, {0x42, 0x5B, 16},
        {0x42, 0x6B, 16}, {0x42, 0x7B, 48}, {0x42, 0xAB, 32}, {0x42, 0xCD, 1}};
    REQUIRE(o30_comm::payloads(h.transport) == expected);
    h.hand->service.set_password({1, 2, 3, 4, 5, 6, 7, 8});
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x42, 0, 8, 1, 2, 3, 4, 5, 6, 7, 8});
    h.hand->service.set_sensor_type(2);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x42, 0x4A, 1, 2});
    h.hand->service.set_protocol_interface(1);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x42, 0xCD, 1, 1});
    h.hand->service.set_device_uid(std::vector<uint8_t>(32, 'x'));
    REQUIRE(h.hop->writes.back().si == 0xAB);
    REQUIRE(h.hop->writes.back().payload == std::vector<uint8_t>(32, 'x'));
}

TEST_CASE("comm: O30i product helper rejects shortened ASCII fields", "[comm][o30][alignment][identity]") {
    const auto si = GENERATE(0x00, 0x39, 0x59, 0x61);
    auto h = o30_comm::make_hand();
    h.transport->on_request(1, [](const Message &req) {
        return std::vector<Message>{o30_comm::hop_reply(0x401, 0x41, false, req.data[1], {'O'})};
    });
    if (si == 0) {
        REQUIRE_THROWS_AS(h.hand->version.get_model(), protocol::ProtocolError);
    }
    if (si == 0x39) {
        REQUIRE_THROWS_AS(h.hand->version.get_device_uid(), protocol::ProtocolError);
    }
    if (si == 0x59) {
        REQUIRE_THROWS_AS(h.hand->version.get_protocol_name(), protocol::ProtocolError);
    }
    if (si == 0x61) {
        REQUIRE_THROWS_AS(h.hand->version.get_protocol_version(), protocol::ProtocolError);
    }
}

TEST_CASE("comm: O30i unit string excludes its decimal byte and ranges cover 36 slots",
          "[comm][o30][alignment][unit]") {
    auto h = o30_comm::make_hand();
    auto &object = h.hop->objects.at({0x43, false});
    object.resize(162, 0);
    h.hop->put_ascii(0x43, 1, 16, "abcdefghijklmnop");
    object[17] = 0xFE;
    object[0x9E] = 0x34;
    object[0x9F] = 0x12;
    object[0xA0] = 0xCD;
    object[0xA1] = 0xAB;
    const auto info = h.hand->unit.query(0x20);
    REQUIRE(info.unit == "abcdefghijklmnop");
    REQUIRE(info.decimal_places == 0xFE);
    REQUIRE(info.raw[16] == 0xFE);
    REQUIRE(info.target_mi == 0x20);
    REQUIRE(o30_comm::payloads(h.transport) == std::vector<std::vector<uint8_t>>{{0x43, 0, 1, 0x20}, {0x43, 1, 17}});
    const auto range = h.hand->unit.get_range(0x9E);
    REQUIRE(range.low == 0x1234);
    REQUIRE(range.high == 0xABCD);
    REQUIRE(range.target_mi == 0x20);
    h.transport->clear_sent();
    for (uint8_t bad : {0, 1, 17, 19, 159, 255}) {
        REQUIRE_THROWS_AS(h.hand->unit.get_range(bad), linkerbot::hand::ValidationError);
    }
    REQUIRE_THROWS_AS(h.hand->unit.select_target(0x80), linkerbot::hand::ValidationError);
    REQUIRE(h.transport->sent().empty());
}

TEST_CASE("comm: O30i short unit metadata cannot look like a successful query", "[comm][o30][alignment][unit]") {
    auto h = o30_comm::make_hand();
    h.transport->on_request(1, [](const Message &) {
        return std::vector<Message>{o30_comm::hop_reply(0x401, 0x43, false, 1, std::vector<uint8_t>(16, 'a'))};
    });
    REQUIRE_THROWS_AS(h.hand->unit.get_unit(), protocol::ProtocolError);
}

TEST_CASE("comm: O30i legacy sensor channels never silently truncate long data", "[comm][o30][alignment][sensor]") {
    const auto length = GENERATE(size_t{255}, size_t{256}, size_t{257});
    const auto channel = GENERATE(uint8_t{1}, uint8_t{2}, uint8_t{3});
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(0x31, 0, 32, "TACTILE");
    auto &info = h.hop->objects.at({0x31, false});
    info[0x2B] = static_cast<uint8_t>(length);
    info[0x2C] = static_cast<uint8_t>(length >> 8);
    std::vector<uint8_t> bytes(length);
    for (size_t i = 0; i < length; ++i) {
        bytes[i] = static_cast<uint8_t>(i);
    }
    h.hop->put(static_cast<uint8_t>(0x31 + channel), bytes);
    if (length <= 256) {
        REQUIRE(h.hand->sensor.read_channel(channel).payload == bytes);
        if (channel == 1) {
            REQUIRE(h.hand->sensor.read_data().payload == bytes);
        }
    } else {
        REQUIRE_THROWS_AS(h.hand->sensor.read_channel(channel), protocol::ProtocolError);
        REQUIRE_THROWS_AS(h.hand->sensor.read_data(), protocol::ProtocolError);
        for (const auto &req : h.transport->sent()) {
            REQUIRE((req.data[0] == 0x31 || req.data[0] == 0x41));
        }
    }
}

TEST_CASE("comm: O30i NO_SENSOR forbids data requests even with inconsistent length",
          "[comm][o30][alignment][sensor]") {
    auto h = o30_comm::make_hand();
    h.hop->objects.at({0x31, false})[0x2B] = 20;
    REQUIRE_THROWS_AS(h.hand->sensor.read_data(), protocol::ProtocolError);
    for (const auto &req : h.transport->sent()) {
        REQUIRE((req.data[0] == 0x31 || req.data[0] == 0x41));
    }
}

TEST_CASE("comm: O30i presets retain all five object addresses and both write forms",
          "[comm][o30][alignment][preset]") {
    const auto index = GENERATE(uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{4}, uint8_t{5});
    auto h = o30_comm::make_hand();
    const uint8_t mi = static_cast<uint8_t>(0x35 + index);
    h.hop->put(mi, {1, 2, 3, 4, 5, 6, 7});
    const auto data = h.hand->preset.get(index);
    REQUIRE(data.index == index);
    REQUIRE(data.raw == std::array<uint8_t, 7>{1, 2, 3, 4, 5, 6, 7});
    h.hand->preset.set_non_trigger(index, {7, 6, 5, 4, 3});
    REQUIRE(h.hop->objects.at({mi, false}) == std::vector<uint8_t>{7, 6, 5, 4, 3, 6, 7});
    h.hand->preset.set_raw(index, {7, 6, 5, 4, 3, 2, 1});
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{mi, 0, 7, 7, 6, 5, 4, 3, 2, 1});
}

TEST_CASE("comm: O30i documented unsupported mapping responds with explicit errors",
          "[comm][o30][alignment][mapping]") {
    auto h = o30_comm::make_hand();
    h.transport->on_request(1, [](const Message &req) {
        if (req.dlc == 3 && req.data[1] == 0x24) {
            return std::vector<Message>{o30_comm::hop_reply(0x401, 0, false, 0x24, {0, 0})};
        }
        if (req.dlc > 3) {
            return std::vector<Message>{o30_comm::error_reply(0x401, 0x24, 0x40)};
        }
        return std::vector<Message>{};
    });
    const auto mapping = h.hand->mapping.get_length_and_enable();
    REQUIRE(mapping.length == 0);
    REQUIRE(mapping.enabled == 0);
    REQUIRE_THROWS_AS(h.hand->mapping.get_map(10ms), std::runtime_error);
    // No HOP sequence can distinguish an arbitrarily late mapping read from
    // the next operation. Runtime blocks key reuse until physical recovery.
    REQUIRE_THROWS_AS(h.hand->mapping.set_length_and_enable(0, 0), linkerbot::hand::DeviceNotReadyError);
    auto fresh = o30_comm::make_hand();
    fresh.transport->on_request(
        1, [](const Message &) { return std::vector<Message>{o30_comm::error_reply(0x401, 0x24, 0x40)}; });
    REQUIRE_THROWS_AS(fresh.hand->mapping.set_length_and_enable(0, 0), protocol::ProtocolError);
    REQUIRE_THROWS_AS(fresh.hand->mapping.set_map({}), protocol::ProtocolError);
}
