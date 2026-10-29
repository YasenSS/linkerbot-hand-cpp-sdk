#pragma once
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l6/hop_l6.hpp>
#include <map>
#include <mutex>

namespace l6_hop_test {
    using namespace linkerbot;
    struct Sensor {
        std::string descriptor;
        std::vector<uint8_t> data;
        uint8_t rows = 0, columns = 0;
    };
    struct Firmware {
        std::mutex mutex;
        std::array<uint8_t, 256> info{}, positions{};
        std::map<uint8_t, std::array<uint8_t, 256>> registers;
        std::array<Sensor, 5> sensors;
        uint8_t selected = 1;
        size_t position_writes = 0;
        bool hold_tail = false, short_tail = false, no_reply = false, bad_length = false, bad_selected = false;
        bool invalid_mask = false;
        std::optional<can::Message> held;
        static void put(std::array<uint8_t, 256> &target, size_t offset, const std::string &text, size_t width) {
            std::fill_n(target.begin() + offset, width, 0);
            std::copy_n(text.begin(), std::min(width, text.size()), target.begin() + offset);
        }
        Firmware() {
            put(info, 0, "L6", 16);
            put(info, 0x39, "LHL6-03.3-632-R-B-1-D", 32);
            put(info, 0x59, "HOP", 8);
            put(info, 0x61, "1.0.0", 8);
            put(info, 0xa1, "3.0.0", 8);
            put(info, 0x89, "V1.2.0", 16);
            put(info, 0xa9, "3.3", 8);
            put(info, 0xf1, "RIGHT", 8);
            const std::array<uint16_t, 6> raw{256, 32768, 65535, 0, 1, 513};
            for (size_t i = 0; i < 6; ++i) {
                const size_t offset = hand::l6::hop::kJointSlots[i] * 2;
                positions[offset] = raw[i] & 255;
                positions[offset + 1] = raw[i] >> 8;
            }
            for (uint8_t mi : {2, 4, 7, 12}) {
                for (size_t i = 0; i < 6; ++i) {
                    registers[mi][hand::l6::hop::kJointSlots[i]] = static_cast<uint8_t>(i + 1);
                }
            }
            registers[12][10] = 0xb0; // bit7 known, bits4/5 reserved
            sensors[0] = {"FnS_U16_1;Rsv_U16_1;Fn_U8_4", {42, 0, 99, 0, 1, 2, 3, 4}, 2, 2};
            sensors[1] = {"FNS_U16_1;FTS_I16_1;FDS_F32_1", {9, 0, 254, 255, 0, 0, 52, 66}, 0, 0};
            sensors[3] = {"xyz_F32_1", {0, 0, 128, 63, 0, 0, 0, 64, 0, 0, 64, 64}, 0, 0};
            sensors[4] = {"Fn_U16_260", std::vector<uint8_t>(520, 7), 2, 130};
        }
        can::Message response(const can::Message &req, const std::vector<uint8_t> &data) {
            can::Message result;
            result.id = req.id | 0x400;
            result.is_fd = true;
            const size_t count = data.size() + 3;
            for (uint8_t n : {3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64}) {
                if (n >= count) {
                    result.dlc = n;
                    break;
                }
            }
            result.data[0] = req.data[0];
            result.data[1] = req.data[1];
            result.data[2] = data.size();
            std::copy(data.begin(), data.end(), result.data.begin() + 3);
            return result;
        }
        std::vector<can::Message> handle(const can::Message &req) {
            const std::lock_guard<std::mutex> lock(mutex);
            if (no_reply || !req.is_fd || req.is_extended || req.dlc < 3) {
                return {};
            }
            const uint8_t mi = req.data[0] & 0x7f, si = req.data[1], length = req.data[2];
            if (length > 61 || size_t(si) + length > 256) {
                return {};
            }
            const bool write = req.dlc > 3;
            if (write && mi == 0x20) {
                ++position_writes;
            }
            std::vector<uint8_t> data;
            if (mi == 0x41) {
                data.assign(info.begin() + si, info.begin() + si + length);
            } else if (mi == 0x31) {
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
                    meta[43] = sensor.descriptor.size();
                    std::copy(sensor.descriptor.begin(), sensor.descriptor.end(), meta.begin() + 44);
                    meta[108] = sensor.data.size() & 255;
                    meta[109] = sensor.data.size() >> 8;
                    meta[110] = bad_selected ? selected % 5 + 1 : selected;
                    meta[111] = sensor.rows;
                    meta[112] = sensor.columns;
                    meta[113] = invalid_mask;
                    meta[114] = 1;
                    if (bad_length) {
                        ++meta[108];
                    }
                    data.assign(meta.begin() + si, meta.begin() + si + length);
                }
            } else if (mi >= 0x32 && mi <= 0x34) {
                const auto &sensor = sensors[selected - 1];
                const size_t start = (mi - 0x32) * 255 + si;
                if (start + length > sensor.data.size()) {
                    return {};
                }
                data.assign(sensor.data.begin() + start, sensor.data.begin() + start + length);
            } else if (mi == 0x35) {
                data = {1};
            } else {
                auto &object = mi == 0x20 ? positions : registers[mi];
                if (write) {
                    std::copy_n(req.data.begin() + 3, length, object.begin() + si);
                }
                data.assign(object.begin() + si, object.begin() + si + length);
            }
            auto reply = response(req, data);
            if (write && mi == 0x20 && si == 20) {
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
            auto result = std::make_unique<can::FakeTransport>();
            result->on_request(id, [this](const auto &req) { return handle(req); });
            return result;
        }
    };
    inline hand::HandDeviceConfig config(uint32_t id = 1) {
        hand::HandDeviceConfig result;
        result.model = hand::Model::kL6;
        result.side = hand::HandSide::kRight;
        result.l6_protocol = hand::L6Protocol::kHopV004;
        result.address = id;
        result.command_timeout = std::chrono::milliseconds{150};
        return result;
    }
}
