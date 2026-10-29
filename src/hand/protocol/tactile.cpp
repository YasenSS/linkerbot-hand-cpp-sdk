#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/protocol/tactile.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace linkerbot::hand::protocol::hop {
    using o30::SensorField;
    using o30::SensorReading;
    std::vector<SensorField> decode_tactile_fields(const SensorReading &reading, bool extended_semantics) {
        using hand::ProtocolError;
        const auto &text = reading.info.descriptor;
        std::vector<SensorField> fields;
        size_t start = 0, offset = 0;
        while (start < text.size()) {
            const auto end = text.find(';', start);
            const auto token = text.substr(start, end == std::string::npos ? end : end - start);
            const auto a = token.find('_'), b = token.find('_', a + 1);
            if (a == std::string::npos || b == std::string::npos) {
                throw ProtocolError("malformed tactile descriptor");
            }
            SensorField field;
            field.semantic = token.substr(0, a);
            if (extended_semantics) {
                if (field.semantic == "FNS") {
                    field.semantic = "FnS";
                }
                if (field.semantic == "FTS") {
                    field.semantic = "FtS";
                }
                if (field.semantic == "FDS") {
                    field.semantic = "FdS";
                }
            }
            constexpr std::array<std::string_view, 11> semantics{"Fn",  "Ft", "Fd", "Px", "FnS", "FtS",
                                                                 "FdS", "x",  "y",  "z",  "xyz"};
            if (std::find(semantics.begin(), semantics.end(), field.semantic) == semantics.end() &&
                !(extended_semantics && field.semantic == "Rsv")) {
                throw ProtocolError("unknown tactile descriptor semantic");
            }
            field.encoding = token.substr(a + 1, b - a - 1);
            field.offset = offset;
            const auto count_text = token.substr(b + 1);
            if (count_text.empty() || count_text.front() == '0') {
                throw ProtocolError("invalid tactile descriptor count");
            }
            size_t count = 0;
            for (const char digit : count_text) {
                if (digit < '0' || digit > '9' || count > 767) {
                    throw ProtocolError("invalid tactile descriptor count");
                }
                count = count * 10 + static_cast<size_t>(digit - '0');
            }
            field.count = count;
            const auto &type = field.encoding;
            const size_t width = type == "U8" || type == "I8"                      ? 1
                                 : type == "U16" || type == "I16"                  ? 2
                                 : type == "U32" || type == "I32" || type == "F32" ? 4
                                                                                   : 0;
            const size_t values = count * (field.semantic == "xyz" ? 3 : 1);
            if (!width || values > 767 || offset + values * width > reading.payload.size()) {
                throw ProtocolError("tactile descriptor exceeds payload");
            }
            for (size_t i = 0; i < values; ++i) {
                uint32_t bits = 0;
                for (size_t j = 0; j < width; ++j) {
                    bits |= uint32_t{reading.payload[offset++]} << (8 * j);
                }
                double value = bits;
                if (type.front() == 'I' && (bits & (uint32_t{1} << (width * 8 - 1)))) {
                    value = static_cast<double>(bits) - std::ldexp(1.0, width * 8);
                }
                if (type == "F32" && field.semantic != "Rsv") {
                    float number;
                    std::memcpy(&number, &bits, sizeof(number));
                    value = number;
                }
                if (!std::isfinite(value)) {
                    throw ProtocolError("non-finite tactile value");
                }
                if (field.semantic != "Rsv") {
                    field.values.push_back(value);
                }
            }
            fields.push_back(std::move(field));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
            if (start == text.size()) {
                throw ProtocolError("empty tactile descriptor field");
            }
        }
        if (offset != reading.payload.size()) {
            throw ProtocolError("tactile descriptor length does not match payload");
        }
        return fields;
    }
    constexpr std::array<const char *, 5> kSensors{"thumb", "index", "middle", "ring", "pinky"};
    ForceSample descriptor_force_sample(const o30::SensorData &data, SampleMeta meta, bool extended_semantics) {
        ForceSample result;
        result.raw = data.payload;
        result.shape = data.sensors.empty() ? std::vector<size_t>{data.info.rows, data.info.columns}
                                            : std::vector<size_t>{data.sensors.size()};
        result.meta = meta;
        const auto readings = data.sensors.empty()
                                  ? std::vector<o30::SensorReading>{{data.info, data.payload, data.timestamp}}
                                  : data.sensors;
        for (const auto &reading : readings) {
            const auto &info = reading.info;
            if (info.selected < 1 || info.selected > 5) {
                throw hand::ProtocolError("invalid tactile sensor identity");
            }
            TactileSensorSample sensor;
            sensor.sensor_name = kSensors[info.selected - 1];
            sensor.selected_sensor = info.selected;
            sensor.sensor_type = info.type;
            sensor.data_unit = info.unit;
            sensor.descriptor = info.descriptor;
            sensor.rows = info.rows;
            sensor.columns = info.columns;
            sensor.available = info.type != "NO_SENSOR" && info.total_length != 0;
            sensor.raw_data = reading.payload;
            if (sensor.available) {
                // Legacy metadata has no verified field layout. Never guess.
                if (info.descriptor.empty()) {
                    throw UnsupportedOperationError("O30 tactile field decoding requires HOP 0.0.4 descriptors");
                }
                sensor.fields = decode_tactile_fields(reading, extended_semantics);
            }
            result.sensors.push_back(std::move(sensor));
        }
        for (const auto name : kForceFields) {
            ForceFieldSample field;
            field.name = name;
            field.meta = meta;
            bool present = false;
            for (const auto &sensor : result.sensors) {
                field.sensor_names.push_back(sensor.sensor_name);
                std::vector<double> values;
                std::string encoding;
                for (const auto &decoded : sensor.fields) {
                    std::string semantic = decoded.semantic;
                    for (auto &c : semantic) {
                        if (c >= 'A' && c <= 'Z') {
                            c += 'a' - 'A';
                        }
                    }
                    if (semantic != name) {
                        continue;
                    }
                    values.insert(values.end(), decoded.values.begin(), decoded.values.end());
                    if (!encoding.empty()) {
                        encoding += ';';
                    }
                    encoding += decoded.encoding;
                }
                const bool found = !values.empty();
                present |= found;
                field.raw.push_back(found ? std::optional<std::vector<double>>{values} : std::nullopt);
                field.valid_values.push_back(
                    found ? std::optional<std::vector<bool>>{std::vector<bool>(values.size(), true)} : std::nullopt);
                field.encodings.push_back(found ? std::optional<std::string>{encoding} : std::nullopt);
                const bool force = name == "fn" || name == "fns" || name == "ft" || name == "fts";
                field.units.push_back(found && force && !sensor.data_unit.empty()
                                          ? std::optional<std::string>{sensor.data_unit}
                                          : std::nullopt);
            }
            if (present) {
                result.fields.push_back(std::move(field));
            } else {
                result.unsupported_fields.emplace_back(name);
            }
        }
        return result;
    }

}
