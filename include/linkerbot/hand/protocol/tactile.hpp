#pragma once
#include <linkerbot/hand/api/readings.hpp>
namespace linkerbot::hand::protocol::hop {
    // extended_semantics enables the L6 document's FNS/FTS/FDS aliases and Rsv.
    std::vector<o30::SensorField> decode_tactile_fields(const o30::SensorReading &reading,
                                                        bool extended_semantics = false);
    ForceSample descriptor_force_sample(const o30::SensorData &data, SampleMeta meta, bool extended_semantics = false);
}
