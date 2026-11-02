# Automatic Connection

<a id="automatic"></a>

Device discovery reads identity first, then creates a `HandDevice` from a candidate. Discovery sends no angle, torque, enable, or other control commands.
For Linux SocketCAN, [prepare the communication interface](prepare-interface.md) first.

| Operating system | Default backends | Additional requirements |
| --- | --- | --- |
| Linux | SocketCAN, MiniCANFD | Enable the SocketCAN interface; MiniCANFD requires explicit device/channel pairs. |
| Windows | PCAN, SLCAN, MiniCANFD | Install PCAN/MiniCANFD drivers and libraries; SLCAN requires an explicit serial-port allowlist. |

All backends share the total discovery time budget; a failure in one backend does not discard successful candidates from others.
The optional L6 HOP protocol is not included in current automatic discovery; use [Manual configuration](manual.md#socketcan).

## Connecting devices

### Automatically discovering one device

The following is a complete read-only program. `require_one()` requires exactly one candidate in the entire result, with complete identity;
no candidates, multiple candidates, and partial identity produce different exceptions.

```cpp
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/adapter_transport.hpp>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

// Attempt to close the same device object after either success or failure, preserving the original exception.
void read_and_close(HandDevice &hand) {
    std::exception_ptr failure;
    try {
        hand.open();
        const auto angles = hand.get_angles(1s);
        if (!angles.meta.valid) {
            throw std::runtime_error("Angle sample unavailable");
        }
        if (angles.meta.stale) {
            std::cerr << "Angle sample is stale\n";
        }
        for (const auto value : angles.raw) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
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
}

int main() try {
    DiscoveryConfig scan;
    const auto report = discover_hands(scan);
    for (const auto &failure : report.failures()) {
        std::cerr << failure.backend().value_or("unknown") << ": " << failure.message() << '\n';
    }
    const auto &candidate = report.require_one();
    std::cout << "Channel: " << candidate.channel().name() << '\n';
    // Returns a unique_ptr; construction does not open the channel. open() in read_and_close() establishes the connection.
    auto hand = candidate.create_hand_device();
    read_and_close(*hand);
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

Call object methods with `hand->...` and pass a reference using `*hand`. `create_hand_device()` does not open the connection;
`open()` verifies identity again and establishes a baseline. The read-only example can run as provided; confirm the device and task scope before adding motion operations.

### Automatically discovering multiple devices

The following is a complete standalone program. In C++, `verified_candidates()` returns a list of candidate reference wrappers; access each through `.get()`.
This example connects, reads, and closes one device at a time. For concurrent use of a shared bus, see [Device discovery](../reference/discovery.md).

```cpp
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/adapter_transport.hpp>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

// Attempt to close the same device object after either success or failure, preserving the original exception.
void read_and_close(HandDevice &hand) {
    std::exception_ptr failure;
    try {
        hand.open();
        const auto angles = hand.get_angles(1s);
        if (!angles.meta.valid) {
            throw std::runtime_error("Angle sample unavailable");
        }
        if (angles.meta.stale) {
            std::cerr << "Angle sample is stale\n";
        }
        for (const auto value : angles.raw) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
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
}

int main() try {
    DiscoveryConfig scan;
    const auto report = discover_hands(scan);
    for (const auto &failure : report.failures()) {
        std::cerr << failure.backend().value_or("unknown") << ": " << failure.message() << '\n';
    }
    const auto candidates = report.verified_candidates();
    if (candidates.empty()) {
        throw std::runtime_error("No device has complete identity; check failures and partial candidates in the report");
    }
    // Connect and read sequentially; continue after individual failures and report failure through the exit code.
    bool failed = false;
    for (const auto &reference : candidates) {
        const auto &candidate = reference.get();
        std::cout << "Channel: " << candidate.channel().name() << '\n';
        auto hand = candidate.create_hand_device();
        try {
            read_and_close(*hand);
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            failed = true;
        }
    }
    if (failed) {
        return 1;
    }
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

## Restricting backends and channels

Replace `DiscoveryConfig scan;` in a complete program with one of the configuration snippets below; subsequent steps such as `discover_hands(scan)` stay the same.
The model and address come from discovery results; no model-name strings need to be entered manually.

### SocketCAN (Linux)

```cpp
DiscoveryConfig scan;
scan.backends = {"socketcan"};
scan.channels = {"can0"};
scan.total_timeout = 10s;
```

### PCAN (Windows)

By default, available PCAN Classic channels are enumerated. To restrict the channel and specify a dynamic library, use:

```cpp
DiscoveryConfig scan;
scan.backends = {"pcan"};
scan.pcan.channels = {"PCAN_USBBUS1"};
scan.pcan.library_path = "C:/vendor/PCANBasic.dll";
scan.pcan.nominal_bitrate = 1'000'000;
```

To probe FD, switch to `probe_protocols = {TransportKind::kCanFd}` and configure
`data_bitrate` and `fd_timing` for your hardware. PCAN FD is supported in code, but check the [Validation scope](../reference/compatibility.md).

### SLCAN (Windows)

<a id="slcan"></a>

C++ does not automatically access serial ports using Python's VID/PID rules. Specify the actual COM ports in `scan.slcan.channels`;
the common `scan.channels` setting only filters results and cannot replace the allowlist. Without an allowlist, discovery returns `allowlist_required`.
The following uses CANable FD firmware on a 1M/5M bus:

```cpp
DiscoveryConfig scan;
scan.backends = {"slcan"};
scan.slcan.channels = {"COM5"};
scan.slcan.protocol = SLCANProtocol::kCanableFd;
scan.slcan.probe_protocols = {TransportKind::kCanFd};
scan.slcan.serial_baud = 115200;
scan.slcan.nominal_bitrate = 1'000'000;
scan.slcan.data_bitrate = 5'000'000;
```

For Classic Lawicel firmware, use `SLCANProtocol::kLawicelClassic` and
`probe_protocols = {TransportKind::kCan}`; this combination does not support FD.
The CANable FD extension requires a compatible transport library; changing the backend name alone does not establish protocol compatibility.

### MiniCANFD

<a id="minicanfd"></a>

On Linux, specify confirmed `(device_index, channel_index)` pairs. Without this configuration, discovery may return
`channel_layout_required`. It does not automatically determine the channel count or fall back from channel 0 to channel 1; explicitly list both entries to check both channels.

```cpp
DiscoveryConfig scan;
scan.backends = {"minicanfd"};
scan.minicanfd.library_path = "/opt/minicanfd/libcanbus.so";
scan.minicanfd.channels = {{0, 0}, {0, 1}};
scan.minicanfd.nominal_bitrate = 1'000'000;
scan.minicanfd.data_bitrate = 5'000'000;
```

On Windows, you may omit `channels` and let the vendor library enumerate available channels. In explicit entries, the first value is the vendor channel number
and the second is fixed at `0`, for example `{{0, 0}, {1, 0}}`. Set the library path to the actual `HCanbus.dll` path.
Discovery results bind the actual module and configuration; opening them later does not fall back to SocketCAN.

## Partial identity and diagnostics

`report.candidates()` includes complete and partial candidates; use `partial_candidates()` to display partial identities separately.
You may fill in the hand side after manual confirmation only when every issue is `missing_side`; do not ignore model or address conflicts.
This snippet also replaces the part of a complete program that creates `hand`. The `report` must come from the current discovery run:

```cpp
DiscoveryConfig scan;
const auto report = discover_hands(scan);
const auto &candidate = report.require_one(true);
HandRuntimeConfig runtime;
runtime.identity_timeout = 2s;
auto hand = candidate.create_hand_device(runtime, HandSide::kLeft);
// Continue with read_and_close(*hand); the hand side must come from confirmed deployment information.
```

Also inspect `code()`, `message()`, `backend()`, and `native_channel()` in `report.failures()`.
Common causes include no response, an inactive interface, a busy channel, a missing dynamic library, a missing serial-port allowlist, or an exhausted discovery time budget.
For complete error-code descriptions, see [Device discovery diagnostics](../reference/discovery.md#failure-diagnostics).

If closing fails, retain the same device object and retry `close()` after resolving the driver or connection fault;
do not treat a cleanup error as a closed connection or immediately open the same device again. The command-line programs above log close errors and exit;
long-running applications should retain the object in their own connection manager and schedule cleanup retries.

After connecting, use the [Model tutorials](../tutorials/l6.md), or choose the appropriate model:
[O6](../tutorials/o6.md), [L20 Lite](../tutorials/l20lite.md), [L20](../tutorials/l20.md),
[L30](../tutorials/l30.md), or [O30](../tutorials/o30.md).

[Back to the documentation overview](../README.md)
