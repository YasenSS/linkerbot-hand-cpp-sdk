#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>

#include "../../../src/hand/unified/detail/force.hpp"
#include "../../../src/hand/unified/detail/model_adapters.hpp"
#include "hand/support/temp_path.hpp"
#include <linkerbot/hand/angle_mapping.hpp>
#include <linkerbot/hand/errors.hpp>

using namespace linkerbot::hand;

TEST_CASE("renamed joints retain legacy calibration only in the original order", "[comm][force-fields]") {
    for (const auto model : {Model::kL20Lite, Model::kL20}) {
        const auto &caps = declared_capabilities(model);
        std::vector<std::string> canonical;
        for (const auto &joint : caps.joints) {
            canonical.emplace_back(joint.name);
        }
        auto legacy = canonical;
        if (model == Model::kL20Lite) {
            legacy[1] = "thumb_abd";
            legacy[9] = "thumb_yaw";
        } else {
            legacy[0] = "thumb_abd";
            legacy[1] = "thumb_yaw";
            for (const size_t index : {2, 5, 8, 11, 14}) {
                legacy[index] += '1';
            }
        }
        const std::string name = model == Model::kL20Lite ? "l20lite" : "l20";
        const auto path = test_util::unique_mapping_path();
        AngleMappingManager old{name, legacy, path};
        auto mapping = default_angle_mapping(legacy.size());
        mapping[2][7] = 42;
        old.set_mapping(mapping);
        AngleMappingManager migrated{name, canonical, path};
        REQUIRE(migrated.get_mapping() == mapping);
        const auto wrong_path = test_util::unique_mapping_path();
        std::swap(legacy[0], legacy[1]);
        AngleMappingManager reordered{name, legacy, wrong_path};
        REQUIRE_THROWS_AS((AngleMappingManager{name, canonical, wrong_path}), ValidationError);
    }
}

TEST_CASE("all models declare canonical tactile quantities", "[comm][force-fields]") {
    for (const auto model : supported_models()) {
        const auto &caps = declared_capabilities(model);
        REQUIRE(caps.force_fields().size() == 7);
        REQUIRE(caps.force_field("fn").query_channel == TelemetrySemantic::kForce);
        for (const auto name : kForceFields) {
            const auto field = caps.force_field(name);
            if (model == Model::kO30) {
                REQUIRE_FALSE(field.supported.has_value());
            } else if (model == Model::kL20Lite) {
                REQUIRE(field.supported == (name != "ft" && name != "fd"));
            } else {
                REQUIRE(field.supported == (name == "fn"));
            }
        }
        REQUIRE_THROWS_AS(caps.force_field("Fn"), ValidationError);
    }
}

TEST_CASE("matrix force projects five fingers without changing protocol bytes", "[comm][force-fields]") {
    const auto verify = [](auto source, Model model) {
        using Data = typename decltype(source.force_sensor)::value_type;
        Data data;
        const auto now = std::chrono::steady_clock::now();
        size_t index = 0;
        const auto fill = [&](auto &finger) {
            finger.timestamp = now;
            for (auto &row : finger.values) {
                row.fill(static_cast<uint8_t>(index));
            }
            ++index;
        };
        fill(data.thumb);
        fill(data.index);
        fill(data.middle);
        fill(data.ring);
        fill(data.pinky);
        source.force_sensor = data;
        source.timestamp = now;
        const auto result =
            unified_detail::unified_snapshot(source, model, HandSide::kUnknown, declared_capabilities(model));
        REQUIRE(result.force.has_value());
        const auto &force = *result.force;
        const size_t rows = model == Model::kO6 ? 10 : 12;
        const size_t columns = model == Model::kO6 ? 4 : 6;
        REQUIRE(force.raw.size() == 5 * rows * columns);
        REQUIRE(force.shape == std::vector<size_t>{5, rows, columns});
        REQUIRE(force.tactile.size() == 5);
        const auto *fn = force.field("fn");
        REQUIRE(fn != nullptr);
        REQUIRE(fn->sensor_names == std::vector<std::string>{"thumb", "index", "middle", "ring", "pinky"});
        for (size_t finger = 0; finger < 5; ++finger) {
            REQUIRE(fn->raw[finger] == std::vector<double>(rows * columns, static_cast<double>(finger)));
            REQUIRE(fn->valid_values[finger] == std::vector<bool>(rows * columns, true));
            REQUIRE(force.tactile[finger].raw[0] == std::vector<double>(columns, static_cast<double>(finger)));
        }
        REQUIRE(force.status("fn") == "available");
        REQUIRE(force.status("fns") == (model == Model::kL20Lite ? "not_read" : "unsupported"));
        REQUIRE_THROWS_AS(force.field("FN"), ValidationError);
    };
    verify(l6::Snapshot{}, Model::kL6);
    verify(o6::Snapshot{}, Model::kO6);
    verify(l20lite::Snapshot{}, Model::kL20Lite);
    verify(l20::Snapshot{}, Model::kL20);
    verify(l30::Snapshot{}, Model::kL30);
}

TEST_CASE("L20Lite scalar fields preserve undefined direction and independent ages", "[comm][force-fields]") {
    const auto now = std::chrono::steady_clock::now();
    l20lite::Snapshot source;
    source.timestamp = now;
    l20lite::AllFingersData matrix;
    for (auto *finger : {&matrix.thumb, &matrix.index, &matrix.middle, &matrix.ring, &matrix.pinky}) {
        finger->timestamp = now - std::chrono::seconds{10};
    }
    source.force_sensor = matrix;
    std::array<l20lite::TipFinger, 5> fingers{};
    for (auto &finger : fingers) {
        finger = {0, 20, 255, 30};
    }
    fingers[1].direction_raw = 127;
    source.tip_force = l20lite::TipForceData{l20lite::TipForce::from_array(fingers), now};
    auto force = test_support::require_value(unified_detail::unified_snapshot(source, Model::kL20Lite, HandSide::kLeft,
                                                                              declared_capabilities(Model::kL20Lite))
                                                 .force);
    REQUIRE(force.fields.size() == 5);
    REQUIRE(force.tactile.size() == 5);
    REQUIRE(force.raw.size() == 360);
    REQUIRE(force.field("fn")->meta.collected_at != force.field("fns")->meta.collected_at);
    REQUIRE(force.field("fns")->raw[0] == std::vector<double>{0});
    REQUIRE(force.field("fds")->raw[0] == std::vector<double>{255});
    REQUIRE(force.field("fds")->valid_values[0] == std::vector<bool>{false});
    REQUIRE(force.field("fds")->valid_values[1] == std::vector<bool>{true});
    REQUIRE(force.status("fns") == "available");
    REQUIRE(force.status("ft") == "unsupported");
    unified_detail::complete_force_meta(force, 7, declared_capabilities(Model::kL20Lite));
    REQUIRE(force.field("fn")->meta.connection_generation == 7);
    REQUIRE(test_support::require_value(force.field("fn")->meta.age) >
            test_support::require_value(force.field("fns")->meta.age));
    REQUIRE(unified_detail::normal_force_resultant(force).raw == std::vector<double>(5, 0));
}

TEST_CASE("O30 descriptors decode raw quantities with absent fingers", "[comm][force-fields]") {
    o30::SensorData data;
    const auto now = std::chrono::steady_clock::now();
    data.timestamp = now;
    for (uint8_t finger = 1; finger <= 5; ++finger) {
        o30::SensorReading reading;
        reading.info.selected = finger;
        reading.info.type = finger == 3 ? "NO_SENSOR" : "TEST";
        reading.info.unit = "g";
        reading.info.descriptor = finger == 3 ? "" : "FnS_U16_1;Fn_U8_2;Ft_I16_1;FdS_F32_1;Px_U32_1";
        reading.payload = finger == 3
                              ? std::vector<uint8_t>{}
                              : std::vector<uint8_t>{0, 1, 0, 9, 0xFE, 0xFF, 0, 0, 0x80, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF};
        reading.info.total_length = static_cast<uint16_t>(reading.payload.size());
        reading.timestamp = now;
        data.sensors.push_back(reading);
    }
    SampleMeta meta;
    meta.valid = true;
    meta.collected_at = now;
    const auto force = unified_detail::descriptor_force_sample(data, meta);
    REQUIRE(force.sensors.size() == 5);
    REQUIRE(force.field("fns")->raw[0] == std::vector<double>{256});
    REQUIRE(force.field("fn")->raw[0] == std::vector<double>{0, 9});
    REQUIRE(force.field("ft")->raw[0] == std::vector<double>{-2});
    REQUIRE(force.field("fds")->raw[0] == std::vector<double>{1});
    REQUIRE(force.field("px")->raw[0] == std::vector<double>{4294967295.0});
    REQUIRE(force.field("fns")->units[0] == "g");
    REQUIRE_FALSE(force.field("fds")->units[0].has_value());
    REQUIRE_FALSE(force.field("px")->units[0].has_value());
    REQUIRE_FALSE(force.field("fn")->raw[2].has_value());
    REQUIRE_FALSE(force.field("fn")->valid_values[2].has_value());
    REQUIRE(force.status("ft") == "available");
    REQUIRE(force.status("fd") == "unsupported");
    auto invalidated = force;
    for (auto &field : invalidated.fields) {
        field.meta.valid = false;
    }
    REQUIRE(invalidated.status("fn") == "invalidated");
    REQUIRE_THROWS_AS(unified_detail::normal_force_resultant(force), UnsupportedOperationError);
    data.sensors[0].payload.pop_back();
    REQUIRE_THROWS_AS(unified_detail::descriptor_force_sample(data, meta), o30::protocol::ProtocolError);
}

TEST_CASE("O30 descriptors reject non-finite readings", "[comm][force-fields]") {
    o30::SensorData data;
    data.info.selected = 1;
    data.info.type = "TEST";
    data.info.total_length = 4;
    data.info.descriptor = "FnS_F32_1";
    data.payload = {0, 0, 0x80, 0x7F};
    REQUIRE_THROWS_AS(unified_detail::descriptor_force_sample(data, {}), o30::protocol::ProtocolError);
}
