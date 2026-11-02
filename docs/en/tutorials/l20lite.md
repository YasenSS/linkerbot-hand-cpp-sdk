# L20Lite Feature Guide

L20Lite uses Classic CAN and has 10 joints.
This page uses C++ `HandDevice` for common operations. The examples follow the current C++ SDK API.

## Contents

- [Usage](#usage)
- [Joint order](#joint-order)
- [Device identity](#identity)
- [Capabilities](#capabilities)
- [Angle control](#angle-control)
- [Speed control](#speed-control)
- [Acceleration settings](#acceleration)
- [Reading sensor data](#sensor-reading)

<a id="usage"></a>

## Usage

First configure your environment using [Prepare the communication interface](../connections/prepare-interface.md), then choose [Automatic connection](../connections/automatic.md)
or [Manual connection](../connections/manual.md). The complete read-only Linux SocketCAN program below
provides the `hand` object, chrono literals, and printing/waiting helpers used by the snippets later on this page.
For Windows, replace the device construction code with a verified adapter factory; the feature snippets remain the same.

```cpp
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <linkerbot/hand/api/device.hpp>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

template<class Values>
void print_values(const Values &values) {
    for (const auto value : values) {
        std::cout << +value << ' ';
    }
    std::cout << '\n';
}

void check_sample(const SampleMeta &meta) {
    if (!meta.valid) {
        throw std::runtime_error("Sample unavailable");
    }
    if (meta.stale) {
        std::cerr << "Sample is stale; read it again as required by your application\n";
    }
}

void wait_command(const CommandHandle &command) {
    if (!command.wait_for(2s)) {
        throw std::runtime_error("Wait timed out; command was not cancelled and is not automatically resent");
    }
    if (const auto error = command.error()) {
        std::rethrow_exception(error);
    }
    const auto stage = command.stage();
    if (stage != CommandStage::kAcknowledged &&
        !(stage == CommandStage::kDispatched && command.ack_expectation() == AckExpectation::kNone)) {
        throw std::runtime_error("Command did not complete successfully; it may have been replaced or cancelled");
    }
}

void print_sample(const RawVectorSample &sample) {
    check_sample(sample.meta);
    print_values(sample.raw);
}

void print_field(const ForceSample &force, const std::string &name) {
    const auto *field = force.field(name);
    if (!field) {
        std::cout << name << ": " << force.status(name) << '\n';
        return;
    }
    check_sample(field->meta);
    for (size_t finger = 0; finger < field->raw.size(); ++finger) {
        if (!field->raw[finger]) {
            std::cout << "finger " << finger << ": no data\n";
            continue;
        }
        const auto &values = *field->raw[finger];
        for (size_t point = 0; point < values.size(); ++point) {
            if (finger < field->valid_values.size() && field->valid_values[finger] &&
                point < field->valid_values[finger]->size() && !(*field->valid_values[finger])[point]) {
                std::cout << "invalid ";
            } else {
                std::cout << values[point] << ' ';
            }
        }
        std::cout << '\n';
    }
}

int main() try {
    HandDeviceConfig config;
    config.model = Model::kL20Lite;
    config.side = HandSide::kLeft; // Set the actual hand side.
    config.interface_name = "can0"; // This complete program uses Linux SocketCAN.
    HandDevice hand(config);
    std::exception_ptr failure;
    try {
        hand.open();
        // Insert the feature snippet you need here; do not run every control example at once.
        const auto angles = hand.get_angles(1s);
        check_sample(angles.meta);
        print_values(angles.raw);
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        hand.close();
    } catch (const std::exception &error) {
        if (!failure) {
            throw;
        }
        std::cerr << "Failed to close connection: " << error.what() << '\n';
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

Place each ordinary C++ snippet below after `hand.open()` succeeds and before `hand.close()`,
keeping these helper functions. The snippets are not standalone programs, and you should not run all control operations at once.
The sections on native Managers provide separate standalone programs.

Unified setters and discrete operations such as `enable()` / `disable()` return a `CommandHandle`.
`wait_command()` waits for termination, then checks the error and final stage: only `kAcknowledged`, or `kDispatched` when no ACK is expected,
counts as completed communication under the corresponding protocol. Replacement, cancellation, and failure are not success; a wait timeout neither cancels nor automatically resends the command.
Your application must separately verify mechanical positioning and control effects.

Check `meta.valid` first, then use `meta.stale` to determine whether a sample has expired; a cached reading is not a live measurement.
If closing fails, a long-running application should retain the same object and schedule cleanup retries rather than immediately opening another connection.

## Joint order

The SDK reads and sets joint data in the following order.

| Number | Joint name | Joint |
| ---: | --- | --- |
| 1 | `thumb_flex` | Thumb flexion |
| 2 | `thumb_roll` | Thumb roll |
| 3 | `index_flex` | Index finger flexion |
| 4 | `middle_flex` | Middle finger flexion |
| 5 | `ring_flex` | Ring finger flexion |
| 6 | `pinky_flex` | Little finger flexion |
| 7 | `index_abd` | Index finger abduction/adduction |
| 8 | `ring_abd` | Ring finger abduction/adduction |
| 9 | `pinky_abd` | Little finger abduction/adduction |
| 10 | `thumb_rotation` | Thumb rotation |

Table numbering starts at 1; C++ vector indices are `0..9`. Use `joints[index].name` in the capabilities as the authoritative name.

<a id="identity"></a>

## Device identity

`get_device_info(1s)` actively reads and verifies device identity. C++ returns a struct; inspect its fields rather than printing the whole object directly.
`get_identity()` only reads the identity cache for the current connection generation.

```cpp
const auto info = hand.get_device_info(1s);
std::cout << "Serial number: " << info.serial_number.value_or("Not reported by device") << '\n';
std::cout << "Product: " << info.product_code.value_or("Not reported by device") << '\n';
```

<a id="capabilities"></a>

## Capabilities

`get_capabilities()` sends no control commands. Use the capabilities to confirm whether the current model/protocol supports an operation,
especially across different firmware, protocols, and sensors for the same model. Joint indices start at 0; table numbering starts at 1.

```cpp
const auto caps = hand.get_capabilities();
std::cout << caps.serialize() << '\n';
for (const auto &joint : caps.joints) {
    std::cout << joint.index << ": " << joint.name << " raw="
              << joint.raw_angle_range.minimum << ".." << joint.raw_angle_range.maximum << '\n';
}
```

<a id="angle-control"></a>

## Angle control

Raw angles range from `0..255`; by default, percentages increase in the same direction as raw values. Determine the mechanical pose from the joint, hand side, and actual angle mapping;
do not simply equate the endpoints of an abduction/adduction or rotation axis with "bent/straight".

### Reading angles

`AngleSample::raw` is a `vector<double>`; `percent` contains an `optional<double>` for each joint.
Do not replace an unavailable percentage conversion with 0.

```cpp
const auto sample = hand.get_angles(1s);
check_sample(sample.meta);
print_values(sample.raw);
for (const auto &percent : sample.percent) {
    if (percent) {
        std::cout << *percent << ' ';
    } else {
        std::cout << "unavailable ";
    }
}
std::cout << '\n';
```

### Setting raw angles

`set_angles()` takes 10 `int32_t` values in joint-table order. This example reads the current values and uses them as targets;
in your application, replace them with confirmed targets within the allowed ranges. Control frames are still sent, so the device may move.

```cpp
const auto sample = hand.get_angles(1s);
check_sample(sample.meta);
std::vector<int32_t> targets(sample.raw.begin(), sample.raw.end());
wait_command(hand.set_angles(targets));
const auto after = hand.get_angles(1s);
check_sample(after.meta);
print_values(after.raw);
```

### Setting percentage angles

`set_percent_angles()` takes 10 `float` values in the range `0..100`. `50.0F` is only the midpoint of each axis mapping;
it is not a universally validated safe pose. Replace it to suit the actual task.

```cpp
std::vector<float> targets(10, 50.0F);
wait_command(hand.set_percent_angles(targets));
```

<a id="speed-control"></a>

## Speed control

### Reading measured speeds

Actively read the current speeds; measured speeds and configured values are different.

```cpp
print_sample(hand.get_speeds(1s));
```

### Setting speeds

The raw setting range is `0..255`.

```cpp
wait_command(hand.set_speeds(std::vector<double>(10, 100.0)));
```

`get_configured_speeds()` only reads cached complete write responses and does not issue a query. The cache must be rebuilt after reconnecting.

```cpp
print_sample(hand.get_configured_speeds(1s));
```

<a id="acceleration"></a>

## Acceleration settings

### Setting accelerations

`set_accelerations()` takes 10 raw counts in the range `0..254`; do not interpret them directly as physical acceleration.

```cpp
wait_command(hand.set_accelerations(std::vector<double>(10, 100.0)));
```

### Reading configured accelerations

L20 Lite only reads cached complete write responses; set the accelerations first in the same connection.

```cpp
print_sample(hand.get_configured_accelerations(1s));
```

<a id="sensor-reading"></a>

## Reading sensor data

### Reading currents

`get_currents()` reads raw measured-current counts, which differ from current-limit settings. Do not label them as mA without a reliable conversion factor.

```cpp
print_sample(hand.get_currents(1s));
```

### Reading temperatures

The capabilities for this model declare degrees Celsius as the temperature unit; you must still check sample validity.

```cpp
print_sample(hand.get_temperatures(1s));
```

### Reading tactile matrices

In C++, select fields with `get_force(vector<string>{"fn"}, timeout)` and access them through `field("fn")`,
rather than Python's `force.fn.raw`. Each optional vector in `raw` corresponds to one finger; `nullopt` means missing data,
and `false` in `valid_values` means an invalid value. The `print_field()` helper above displays this information without filling in zeros or computing sums.

```cpp
const auto force = hand.get_force(std::vector<std::string>{"fn"}, 1s);
print_field(force, "fn");
```

The default matrix layout is 5 fingers with 12×6 points per finger. The returned fields and `force.tactile` retain the actual data; use the current capabilities and firmware as the reference.

The validated V4.6.10 firmware does not support some tactile scalar queries. Check the capabilities and returned status after connecting;
do not interpret missing fields or unsupported-operation exceptions as the numeric value 0.

### Reading normal-force summaries

Request the `fns` field only if the current firmware supports it.

```cpp
const auto force = hand.get_force(std::vector<std::string>{"fns"}, 1s);
print_field(force, "fns");
```

### Reading tangential-force summaries

Request the `fts` field only if the current firmware supports it.

```cpp
const auto force = hand.get_force(std::vector<std::string>{"fts"}, 1s);
print_field(force, "fts");
```

### Reading tangential directions

Request the `fds` field only if the current firmware supports it. A value of 255 in `fds` means the direction is undefined; interpret it together with the validity mask.

```cpp
const auto force = hand.get_force(std::vector<std::string>{"fds"}, 1s);
print_field(force, "fds");
```

### Reading proximity values

Request the `px` field only if the current firmware supports it.

```cpp
const auto force = hand.get_force(std::vector<std::string>{"px"}, 1s);
print_field(force, "px");
```

### Reading default tactile fields

Omitting the fields uses the SDK's default field policy; this does not mean every firmware version provides every field.

```cpp
const auto force = hand.get_force(1s);
for (const auto &field : force.fields) {
    print_field(force, field.name);
}
```

For more features, firmware limitations, and physical units, see [Model capabilities](../reference/model-capabilities.md).

[Back to the documentation overview](../README.md)
