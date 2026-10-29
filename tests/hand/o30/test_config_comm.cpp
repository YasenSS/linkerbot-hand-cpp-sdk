#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o30/preset.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

#include "hand/o30/comm_helpers.hpp"

using linkerbot::hand::o30::protocol::kMiConfig;
using linkerbot::hand::o30::protocol::kMiMapping;
using linkerbot::hand::o30::protocol::kMiPreset1;
using linkerbot::hand::o30::protocol::kMiService;
using linkerbot::hand::o30::protocol::kMiUnit;
using linkerbot::hand::o30::protocol::kPresetBytes;
using linkerbot::hand::o30::protocol::kPresetNonTriggerBytes;

namespace {
    constexpr auto kTimeout = o30_comm::kTimeout;
} // namespace

// FakeTransport：配置 / 预设 / 单位 / 映射 / 工程服务。危险操作必须 confirm。

// 读 31 字节配置并解析标准 ID / 扩展 ID / 控制模式。
TEST_CASE("comm: config get decodes the 31-byte object", "[comm][o30][config]") {
    auto h = o30_comm::make_hand();
    std::vector<uint8_t> raw(31, 0);
    raw[0] = 0x01;
    raw[1] = 0x00;
    raw[2] = 0x78;
    raw[3] = 0x56;
    raw[4] = 0x34;
    raw[5] = 0x12;
    raw[0x18] = 2;
    raw[0x19] = 1;
    raw[0x1A] = 0;
    h.hop->put(kMiConfig, raw);

    const auto cfg = h.hand->config.get(kTimeout);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({kMiConfig, 0x00, 31}));
    REQUIRE(cfg.standard_id() == 0x0001);
    REQUIRE(cfg.extended_id() == 0x12345678);
    REQUIRE(cfg.control_mode() == 2);
    REQUIRE(cfg.led() == 1);
}

// 单字段写入：标准 ID 小端 2 字节。
TEST_CASE("comm: config writes a single field at a time", "[comm][o30][config]") {
    auto h = o30_comm::make_hand();
    h.hand->config.set_standard_id(0x0123, kTimeout);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({kMiConfig, 0x00, 0x02, 0x23, 0x01}));

    REQUIRE(h.hand->request_id() == 0x123);
    h.hop->request_id = 0x123;
    o30_comm::attach_hop(h.transport, h.hop);
    h.hand->config.set_led(1, kTimeout);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({kMiConfig, 0x19, 0x01, 0x01}));
}

// restore / save 必须 confirm=true，否则不上总线。
TEST_CASE("comm: destructive config writes require confirm", "[comm][o30][config]") {
    auto h = o30_comm::make_hand();
    REQUIRE_THROWS_AS(h.hand->config.restore_config(false, kTimeout), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->config.save_to_flash(false, kTimeout), linkerbot::hand::ValidationError);
    REQUIRE(h.transport->sent().empty());

    h.hand->config.save_to_flash(true, kTimeout);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({kMiConfig, 0x1E, 0x01, 0x01}));
}

// 预设 1..5 对应 MI 0x36..0x3A；只写前 5 个非触发字节。
TEST_CASE("comm: presets map index 1..5 onto MI 0x36..0x3A", "[comm][o30][config]") {
    auto h = o30_comm::make_hand();
    h.hop->put(kMiPreset1, {1, 2, 3, 4, 5, 6, 7});

    const auto preset = h.hand->preset.get(1, kTimeout);
    REQUIRE(preset.raw[0] == 1);
    REQUIRE(preset.non_trigger()[4] == 5);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({kMiPreset1, 0x00, kPresetBytes}));

    std::array<uint8_t, kPresetNonTriggerBytes> fields{9, 8, 7, 6, 5};
    h.hand->preset.set_non_trigger(1, fields, kTimeout);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({kMiPreset1, 0x00, 0x05, 9, 8, 7, 6, 5}));

    REQUIRE_THROWS_AS(h.hand->preset.get(0, kTimeout), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->preset.get(6, kTimeout), linkerbot::hand::ValidationError);
}

// 单位：先写目标 MI，再读 SI 0x01 的 17 字节字符串。
TEST_CASE("comm: unit query writes the target MI then reads the label", "[comm][o30][config]") {
    auto h = o30_comm::make_hand();
    const auto info = h.hand->unit.query(0x01, kTimeout);
    REQUIRE(info.target_mi == 0x01);
    REQUIRE(info.unit.find("normalization") != std::string::npos);

    const auto range = h.hand->unit.get_range(0x12, kTimeout);
    REQUIRE(range.low == 0x0000);
    REQUIRE(range.high == 0x00FF);
}

// 映射 API 仍在：读 36 槽、写 length/enable 两字节。
TEST_CASE("comm: mapping reads 36 slots and the length/enable pair", "[comm][o30][config]") {
    auto h = o30_comm::make_hand();
    h.hop->put(kMiMapping, o30_comm::ramp_slots());
    auto extra = o30_comm::ramp_slots();
    extra.resize(0x26, 0);
    extra[0x24] = 20;
    extra[0x25] = 1;
    h.hop->put(kMiMapping, extra);

    const auto map = h.hand->mapping.get_map(kTimeout);
    REQUIRE(map[5] == 5);
    const auto flags = h.hand->mapping.get_length_and_enable(kTimeout);
    REQUIRE(flags.length == 20);
    REQUIRE(flags.enabled == 1);

    h.hand->mapping.set_length_and_enable(20, 1, kTimeout);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({kMiMapping, 0x24, 0x02, 20, 1}));
}

// 工程服务按字段 SI 读；写左右手是单字节。
TEST_CASE("comm: service status reads documented fields", "[comm][o30][config]") {
    auto h = o30_comm::make_hand();
    auto &object = h.hop->objects[{kMiService, false}];
    object[0x49] = 1;
    object[0x4A] = 2;
    object[0x45] = 0x78;
    object[0x46] = 0x56;
    object[0x47] = 0x34;
    object[0x48] = 0x12;

    const auto status = h.hand->service.get_status(kTimeout);
    REQUIRE(status.hand_side == 1);
    REQUIRE(status.sensor_type == 2);
    REQUIRE(status.heartbeat == 0x12345678);

    h.hand->service.set_hand_side(0, kTimeout);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({kMiService, 0x49, 0x01, 0x00}));

    REQUIRE_THROWS_AS(h.hand->service.set_device_uid({1, 2, 3}, kTimeout), linkerbot::hand::ValidationError);
}
