#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <linkerbot/hand/l20/angle.hpp>
#include <linkerbot/hand/l20/fault.hpp>
#include <linkerbot/hand/l20/speed.hpp>
#include <linkerbot/hand/l20/temperature.hpp>
#include <linkerbot/hand/l20lite/acceleration.hpp>
#include <linkerbot/hand/l20lite/angle.hpp>
#include <linkerbot/hand/l20lite/fault.hpp>
#include <linkerbot/hand/l20lite/speed.hpp>
#include <linkerbot/hand/l20lite/stall.hpp>
#include <linkerbot/hand/l20lite/temperature.hpp>
#include <linkerbot/hand/l6/angle.hpp>
#include <linkerbot/hand/l6/current.hpp>
#include <linkerbot/hand/l6/current_limit.hpp>
#include <linkerbot/hand/l6/fault.hpp>
#include <linkerbot/hand/l6/speed.hpp>
#include <linkerbot/hand/l6/stall.hpp>
#include <linkerbot/hand/l6/temperature.hpp>
#include <linkerbot/hand/l6/torque.hpp>
#include <linkerbot/hand/o6/acceleration.hpp>
#include <linkerbot/hand/o6/angle.hpp>
#include <linkerbot/hand/o6/fault.hpp>
#include <linkerbot/hand/o6/speed.hpp>
#include <linkerbot/hand/o6/stall.hpp>
#include <linkerbot/hand/o6/temperature.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace {
    template <class T> struct JointLayout {
        static constexpr size_t arrays = 1;
        static constexpr bool standard_layout = true;
    };
    // These two legacy types additionally retain reserved fault bits.
    template <> struct JointLayout<linkerbot::hand::l20lite::Fault> {
        static constexpr size_t arrays = 2;
        static constexpr bool standard_layout = false;
    };
    template <> struct JointLayout<linkerbot::hand::o6::Fault> {
        static constexpr size_t arrays = 2;
        static constexpr bool standard_layout = false;
    };
    struct ValidationExpectations {
        std::array<const char *, 3> constructor{};
        std::array<const char *, 3> factory{};
    };
    template <class T, class Array, size_t... Index>
    T construct_values(const Array &values, std::index_sequence<Index...>) {
        return T(values[Index]...);
    }
    template <class Fn> std::string validation_outcome(Fn &&fn) {
        try {
            std::forward<Fn>(fn)();
            return "accepted";
        } catch (const linkerbot::hand::ValidationError &error) {
            return error.what();
        }
    }
    template <class T> void verify_value_contract(const ValidationExpectations &expected) {
        using Array = decltype(T{}.to_array());
        using Value = typename Array::value_type;
        static_assert(sizeof(T) == sizeof(Array) * JointLayout<T>::arrays,
                      "joint layout must retain its scalar storage");
        static_assert(alignof(T) == alignof(Array), "joint alignment must remain unchanged");
        static_assert(std::is_standard_layout<T>::value == JointLayout<T>::standard_layout,
                      "joint values retain their layout category");
        static_assert(std::is_trivially_copyable<T>::value, "joint values retain trivial copying");
        Array values{};
        for (size_t index = 0; index < values.size(); ++index) {
            values[index] = static_cast<Value>(index + 1);
        }
        const auto positional = construct_values<T>(values, std::make_index_sequence<std::tuple_size<Array>::value>{});
        REQUIRE(positional.to_array() == values);
        REQUIRE(T::from_array(values).to_array() == values);
        if constexpr (std::is_floating_point<Value>::value) {
            const std::array<Value, 3> invalid{-1.0F, std::numeric_limits<Value>::infinity(),
                                               std::numeric_limits<Value>::quiet_NaN()};
            for (size_t index = 0; index < invalid.size(); ++index) {
                values[0] = invalid[index];
                REQUIRE(validation_outcome([&] {
                            (void)construct_values<T>(values,
                                                      std::make_index_sequence<std::tuple_size<Array>::value>{});
                        }) == expected.constructor[index]);
                REQUIRE(validation_outcome([&] { (void)T::from_array(values); }) == expected.factory[index]);
            }
        }
    }
}

TEST_CASE("joint value positional adapters preserve field order layout and validation",
          "[comm][contracts][value-parameters]") {
    verify_value_contract<linkerbot::hand::l20::Angle>(
        {{"Angle value out of range [0, 100]", "Angle value out of range [0, 100]",
          "Angle value out of range [0, 100]"},
         {"Angle value out of range [0, 100]", "Angle value out of range [0, 100]",
          "Angle value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::l20::Fault>({});
    verify_value_contract<linkerbot::hand::l20::Speed>(
        {{"Speed value out of range [0, 100]", "Speed value out of range [0, 100]",
          "Speed value out of range [0, 100]"},
         {"Speed value out of range [0, 100]", "Speed value out of range [0, 100]",
          "Speed value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::l20::Temperature>(
        {{"accepted", "accepted", "accepted"}, {"accepted", "accepted", "accepted"}});
    verify_value_contract<linkerbot::hand::l20lite::Acceleration>(
        {{"Acceleration value out of range [0, 100]", "Acceleration value out of range [0, 100]",
          "Acceleration value out of range [0, 100]"},
         {"Acceleration value out of range [0, 100]", "Acceleration value out of range [0, 100]",
          "Acceleration value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::l20lite::Angle>(
        {{"Angle value out of range [0, 100]", "Angle value out of range [0, 100]",
          "Angle value out of range [0, 100]"},
         {"Angle value out of range [0, 100]", "Angle value out of range [0, 100]",
          "Angle value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::l20lite::Fault>({});
    verify_value_contract<linkerbot::hand::l20lite::Speed>(
        {{"Speed value out of range [0, 100]", "Speed value out of range [0, 100]",
          "Speed value out of range [0, 100]"},
         {"Speed value out of range [0, 100]", "Speed value out of range [0, 100]",
          "Speed value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::l20lite::StallTime>(
        {{"StallTime value out of range [0, 2550] ms", "StallTime value out of range [0, 2550] ms",
          "StallTime value out of range [0, 2550] ms"},
         {"StallTime value out of range [0, 2550] ms", "StallTime value out of range [0, 2550] ms",
          "StallTime value out of range [0, 2550] ms"}});
    verify_value_contract<linkerbot::hand::l20lite::StallLimit>(
        {{"StallLimit value out of range [0, 100]", "StallLimit value out of range [0, 100]",
          "StallLimit value out of range [0, 100]"},
         {"StallLimit value out of range [0, 100]", "StallLimit value out of range [0, 100]",
          "StallLimit value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::l20lite::Temperature>(
        {{"accepted", "accepted", "accepted"}, {"accepted", "accepted", "accepted"}});
    verify_value_contract<linkerbot::hand::l6::Angle>(
        {{"Angle value out of range [0, 100]", "Angle value out of range [0, 100]",
          "Angle value out of range [0, 100]"},
         {"Angle value out of range [0, 100]", "Angle value out of range [0, 100]",
          "Angle value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::l6::Current>({});
    verify_value_contract<linkerbot::hand::l6::CurrentLimit>({});
    verify_value_contract<linkerbot::hand::l6::Fault>({});
    verify_value_contract<linkerbot::hand::l6::Speed>(
        {{"Speed value out of range [0, 100]", "Speed value out of range [0, 100]",
          "Speed value out of range [0, 100]"},
         {"Speed value out of range [0, 100]", "Speed value out of range [0, 100]",
          "Speed value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::l6::StallTime>(
        {{"StallTime value out of range [0, 2550] ms", "StallTime value out of range [0, 2550] ms",
          "StallTime value out of range [0, 2550] ms"},
         {"StallTime value out of range [0, 2550] ms", "StallTime value out of range [0, 2550] ms",
          "StallTime value out of range [0, 2550] ms"}});
    verify_value_contract<linkerbot::hand::l6::StallLimit>(
        {{"StallLimit value out of range [0, 1400]", "StallLimit value out of range [0, 1400]",
          "StallLimit value out of range [0, 1400]"},
         {"StallLimit value out of range [0, 1400]", "StallLimit value out of range [0, 1400]",
          "StallLimit value out of range [0, 1400]"}});
    verify_value_contract<linkerbot::hand::l6::Temperature>(
        {{"accepted", "accepted", "accepted"}, {"accepted", "accepted", "accepted"}});
    verify_value_contract<linkerbot::hand::l6::Torque>({});
    verify_value_contract<linkerbot::hand::o6::Acceleration>(
        {{"Acceleration value out of range [0, 100]", "Acceleration value out of range [0, 100]",
          "Acceleration value out of range [0, 100]"},
         {"Acceleration value out of range [0, 100]", "Acceleration value out of range [0, 100]",
          "Acceleration value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::o6::Angle>(
        {{"Angle value out of range [0, 100]", "Angle value out of range [0, 100]",
          "Angle value out of range [0, 100]"},
         {"Angle value out of range [0, 100]", "Angle value out of range [0, 100]",
          "Angle value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::o6::Fault>({});
    verify_value_contract<linkerbot::hand::o6::Speed>(
        {{"Speed value out of range [0, 100]", "Speed value out of range [0, 100]",
          "Speed value out of range [0, 100]"},
         {"Speed value out of range [0, 100]", "Speed value out of range [0, 100]",
          "Speed value out of range [0, 100]"}});
    verify_value_contract<linkerbot::hand::o6::StallTime>(
        {{"StallTime value out of range [0, 2550] ms", "StallTime value out of range [0, 2550] ms",
          "StallTime value out of range [0, 2550] ms"},
         {"StallTime value out of range [0, 2550] ms", "StallTime value out of range [0, 2550] ms",
          "StallTime value out of range [0, 2550] ms"}});
    verify_value_contract<linkerbot::hand::o6::StallLimit>(
        {{"StallLimit value out of range [0, 1000] mA", "StallLimit value out of range [0, 1000] mA",
          "StallLimit value out of range [0, 1000] mA"},
         {"StallLimit value out of range [0, 1000] mA", "StallLimit value out of range [0, 1000] mA",
          "StallLimit value out of range [0, 1000] mA"}});
    verify_value_contract<linkerbot::hand::o6::Temperature>(
        {{"accepted", "accepted", "accepted"}, {"accepted", "accepted", "accepted"}});
}
