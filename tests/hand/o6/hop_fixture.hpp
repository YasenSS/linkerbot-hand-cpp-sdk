#pragma once
#include <algorithm>
#include <array>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/o6/hop_o6.hpp>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace o6_hop_test {
    using namespace linkerbot;
    struct Sensor {
        std::string descriptor;
        std::vector<uint8_t> data;
        uint8_t rows = 0, columns = 0;
    };

    struct Firmware {
        std::mutex mutex;
        bool can_fd = true;
        std::array<uint8_t, 256> info{};
        std::map<uint8_t, std::array<uint8_t, 256>> registers;
        std::array<Sensor, 5> sensors;
        uint8_t selected = 1;
        size_t position_writes = 0;
        bool hold_tail = false, short_tail = false, no_reply = false, bad_length = false, bad_selected = false;
        bool invalid_mask = false;
        bool switch_wire_immediately = false;
        bool drop_mode_write_reply = false;
        bool override_save_ack = false;
        uint8_t save_ack = 0;
        std::optional<can::Message> held;

        static void put(std::array<uint8_t, 256> &target, size_t offset, const std::string &text, size_t width) {
            std::fill_n(target.begin() + static_cast<std::ptrdiff_t>(offset), width, 0);
            std::copy_n(text.begin(), std::min(width, text.size()),
                        target.begin() + static_cast<std::ptrdiff_t>(offset));
        }

        explicit Firmware(bool fd = true) : can_fd(fd) {
            put(info, 0, "LHO6", 16);
            put(info, 0x39, "LHO6-03.3-632-R-B-1-D", 32);
            put(info, 0x59, "HOP", 8);
            put(info, 0x61, "1.0.0", 8);
            put(info, 0xa1, "1.0.3", 8);
            put(info, 0x89, "V0.9.0", 16);
            put(info, 0xa9, "2.0.0", 8);
            put(info, 0xf1, "RIGHT", 8);
            const std::array<uint8_t, 6> raw_pos{10, 51, 255, 0, 128, 200};
            for (size_t i = 0; i < 6; ++i) {
                registers[hand::o6::hop::kMiPosition][hand::o6::hop::kJointSlots[i]] = raw_pos[i];
            }
            for (uint8_t mi :
                 {hand::o6::hop::kMiSpeed, hand::o6::hop::kMiAcceleration, hand::o6::hop::kMiCurrent,
                  hand::o6::hop::kMiTemperature, hand::o6::hop::kMiStallTime, hand::o6::hop::kMiStallThreshold,
                  hand::o6::hop::kMiStallHoldCurrent, hand::o6::hop::kMiFault}) {
                for (size_t i = 0; i < 6; ++i) {
                    registers[mi][hand::o6::hop::kJointSlots[i]] = static_cast<uint8_t>(i + 1);
                }
            }
            registers[hand::o6::hop::kMiFault][10] = 0xb0; // bit7 known, bits4/5 reserved
            auto &can_config = registers[hand::o6::hop::kMiConfig];
            can_config[0] = 0x01;
            can_config[6] = static_cast<uint8_t>(fd ? 2 : 1);
            can_config[7] = 0;
            can_config[8] = 1;
            can_config[9] = 2;
            sensors[0] = {"FnS_U16_1;Rsv_U16_1;Fn_U8_4", {42, 0, 99, 0, 1, 2, 3, 4}, 2, 2};
            sensors[1] = {"FNS_U16_1;FTS_I16_1;FDS_F32_1", {9, 0, 254, 255, 0, 0, 52, 66}, 0, 0};
            sensors[3] = {"xyz_F32_1", {0, 0, 128, 63, 0, 0, 0, 64, 0, 0, 64, 64}, 0, 0};
            sensors[4] = {"Fn_U16_260", std::vector<uint8_t>(520, 7), 2, 130};
        }

        can::Message response(const can::Message &req, const std::vector<uint8_t> &data) const {
            can::Message result;
            result.id = req.id | 0x400U;
            result.is_fd = can_fd;
            const size_t count = data.size() + 3;
            if (can_fd) {
                for (uint8_t n : {3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64}) {
                    if (n >= count) {
                        result.dlc = n;
                        break;
                    }
                }
            } else {
                result.dlc = static_cast<uint8_t>(count);
            }
            result.data[0] = req.data[0];
            result.data[1] = req.data[1];
            result.data[2] = static_cast<uint8_t>(data.size());
            std::copy(data.begin(), data.end(), result.data.begin() + 3);
            return result;
        }

        std::vector<can::Message> handle(const can::Message &req) {
            const std::lock_guard<std::mutex> lock(mutex);
            const size_t max_payload = hand::o6::hop::max_payload_bytes(can_fd);
            if (no_reply || req.is_fd != can_fd || req.is_extended || req.dlc < 3) {
                return {};
            }
            const uint8_t mi = req.data[0] & 0x7fU, si = req.data[1], length = req.data[2];
            if (length == 0 || length > max_payload || size_t(si) + length > 256) {
                return {};
            }
            const bool write = req.dlc > 3;
            if (write && mi == hand::o6::hop::kMiPosition) {
                ++position_writes;
            }
            std::vector<uint8_t> data;
            if (mi == hand::o6::hop::kMiProductInfo) {
                data.assign(info.begin() + si, info.begin() + si + length);
            } else if (mi == hand::o6::hop::kMiSensorCommand) {
                if (write && si == 0x6e) {
                    selected = req.data[3];
                    data = {selected};
                } else {
                    std::array<uint8_t, 256> meta{};
                    const auto &sensor = sensors[selected - 1];
                    put(meta, 0, sensor.data.empty() ? "NO_SENSOR" : "TEST", 32);
                    put(meta, 32, "count", 8);
                    meta[41] = sensor.rows;
                    meta[42] = sensor.columns;
                    meta[43] = static_cast<uint8_t>(sensor.descriptor.size());
                    std::copy(sensor.descriptor.begin(), sensor.descriptor.end(), meta.begin() + 44);
                    meta[108] = static_cast<uint8_t>(sensor.data.size() & 0xffU);
                    meta[109] = static_cast<uint8_t>(sensor.data.size() >> 8U);
                    meta[110] = static_cast<uint8_t>(bad_selected ? selected % 5 + 1 : selected);
                    meta[111] = sensor.rows;
                    meta[112] = sensor.columns;
                    meta[113] = invalid_mask ? 1 : 0;
                    meta[114] = 1;
                    if (bad_length) {
                        ++meta[108];
                    }
                    data.assign(meta.begin() + si, meta.begin() + si + length);
                }
            } else if (mi >= hand::o6::hop::kMiSensorData1 && mi <= hand::o6::hop::kMiSensorData3) {
                const auto &sensor = sensors[selected - 1];
                const size_t start = static_cast<size_t>(mi - hand::o6::hop::kMiSensorData1) * 255 + si;
                if (start + length > sensor.data.size()) {
                    return {};
                }
                data.assign(sensor.data.begin() + static_cast<std::ptrdiff_t>(start),
                            sensor.data.begin() + static_cast<std::ptrdiff_t>(start + length));
            } else {
                auto &object = registers[mi];
                if (write) {
                    std::copy_n(req.data.begin() + 3, length, object.begin() + si);
                }
                data.assign(object.begin() + si, object.begin() + si + length);
            }
            if (write && mi == hand::o6::hop::kMiConfig && si == 0x06 && switch_wire_immediately && !data.empty()) {
                can_fd = data[0] == 2;
            }
            if (write && mi == hand::o6::hop::kMiConfig && si == 0x1E && override_save_ack) {
                data = {save_ack};
            }
            if (drop_mode_write_reply && write && mi == hand::o6::hop::kMiConfig && si == 0x06) {
                return {};
            }
            auto reply = response(req, data);
            if (write && mi == hand::o6::hop::kMiPosition && si == 10) {
                if (short_tail) {
                    reply.data[2] = 1;
                    reply.dlc = 4;
                }
                if (hold_tail) {
                    held = reply;
                    return {};
                }
            }
            return {reply};
        }

        std::unique_ptr<can::FakeTransport> transport(uint32_t id = 1) {
            registers[hand::o6::hop::kMiConfig][0] = static_cast<uint8_t>(id & 0xffU);
            registers[hand::o6::hop::kMiConfig][1] = static_cast<uint8_t>((id >> 8) & 0xffU);
            auto result = std::make_unique<can::FakeTransport>();
            result->on_request(id, [this](const auto &req) { return handle(req); });
            return result;
        }
    };

    inline hand::HandDeviceConfig config(uint32_t id = 1, bool can_fd = true) {
        hand::HandDeviceConfig result;
        result.model = hand::Model::kO6;
        result.side = hand::HandSide::kRight;
        result.o6_protocol = can_fd ? hand::O6Protocol::kHopV004 : hand::O6Protocol::kHopV004Can;
        result.address = id;
        result.command_timeout = std::chrono::milliseconds{150};
        return result;
    }
} // namespace o6_hop_test
