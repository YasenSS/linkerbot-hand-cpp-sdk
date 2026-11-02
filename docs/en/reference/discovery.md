# HandDevice Discovery API

This guide explains the boundaries between device scanning, runtime policy, and final device configuration in the C++ SDK, and how discovery results
can safely create a `HandDevice`.

## Platform defaults

| Platform | Default backends | Notes |
| --- | --- | --- |
| Linux | SocketCAN, MiniCANFD | SocketCAN uses system interfaces; MiniCANFD loads the vendor dynamic library at runtime |
| Windows | PCAN, SLCAN, MiniCANFD | PCAN/MiniCANFD use native adapters; SLCAN supports Lawicel Classic and explicit CANable FD profiles, requiring matching comm backend support |


The default entry point combines backends in table order, sharing one `total_timeout`. Failure of an individual backend, channel,
or adapter does not discard candidates already found by other backends.
The optional L6 HOP CAN FD protocol is not currently included in automatic discovery; explicitly configure its connection as described in [Optional protocol](../guides/usage.md#optional-l6-can-fd-protocol).

## Selecting backends, channels, and custom sources

Default scanning:

```cpp
DiscoveryConfig config;
const auto report = discover_hands(config);
```

Scan only Linux SocketCAN `can0`:

```cpp
DiscoveryConfig config;
config.backends = {"socketcan"};
config.channels = {"can0"};
const auto report = discover_hands(config);
```

Channel filters accept both unified channel names and backend-native names. If an explicitly requested backend or channel
does not exist, discovery reports a failure rather than silently returning no results.

Explicitly combine sources:

```cpp
auto socketcan = std::make_shared<SocketCANDiscoverySource>();
auto minicanfd = std::make_shared<MiniCANFDDiscoverySource>();
CompositeDiscoverySource source{{
    {"socketcan", socketcan},
    {"minicanfd", minicanfd},
}};

DiscoveryConfig config;
const auto report = discover_hands(config, source);
```

PCAN/SLCAN platform integrations or tests can inject
`AdapterChannelEnumerator` and `AdapterTransportFactory` into the corresponding source. Constructing sources and configuration objects
does not enumerate devices, load drivers, or open channels.

## Configuration responsibilities

| Type | Owner | Contents |
| --- | --- | --- |
| `DiscoveryConfig` | Scanning caller | Channel filters, discovery timeout, probe-address ranges, and routing-queue capacity |
| `HandRuntimeConfig` | Device caller | Polling, command/transaction timeouts, recovery policy, angle mapping, and L30 periodic-report policy |
| `HandDeviceConfig` | `HandDevice` | Resolved model, side, interface, address, and all runtime policies |

When creating through discovery, `DiscoveryCandidate` supplies the model, side, interface, device address, and
HostID. `HandRuntimeConfig` lacks these fields, so it cannot override the discovered physical endpoint.
For manual `HandDevice` creation, callers can still fill in the complete `HandDeviceConfig` directly.

## Verified candidate

```cpp
#include <chrono>
#include <linkerbot/hand/discovery.hpp>

using namespace linkerbot::hand;

DiscoveryConfig scan;
scan.channels = {"can0"};
const auto report = discover_hands(scan);
const auto &candidate = report.require_one();

HandRuntimeConfig runtime;
runtime.polling_interval = std::chrono::milliseconds{20};
runtime.identity_timeout = std::chrono::milliseconds{500};
runtime.recovery_attempts = 3;

auto hand = candidate.create_hand_device(runtime);
hand->open();
```

`create_hand_device()` only creates a closed object; it creates no Transport and performs no bus I/O.
`open()` rereads identity and establishes a safe baseline. Candidate capabilities only describe discovery results;
they do not replace identity and capability checks during opening.

## Partial candidate

By default, `make_config()` and `create_hand_device()` accept only Verified candidates.
A Partial candidate can be completed with an explicit side only when all issue codes are `missing_side`:

```cpp
const auto &candidate = report.require_one(true);
auto hand = candidate.create_hand_device(runtime, HandSide::kLeft);
hand->open();
```

Side should come from deployment configuration or user selection. The rules are:

- Only `HandSide::kLeft` or `HandSide::kRight` is accepted;
- Conflicts between the discovered side/Classic CAN address and the requested side are rejected during creation;
- If discovery has no side evidence but the device reports a conflicting side during `open()`, the SDK throws
  `IdentityMismatchError`;
- If the protocol never reports side, the explicit side is treated as caller-confirmed; all other available physical identity fields
  (product code, serial number, device UID) must still match;
- Unknown models, missing nodes/endpoints, address conflicts, or any issue other than `missing_side` still throw
  `IncompleteIdentityError`.

## Identity lifecycle

Discovered identity persists within the `HandDevice` object:

1. Initial `open()` checks model, side, and available product code, serial number, and device UID;
2. Manually created devices pin the same fields after their first successful open;
3. `close()` clears only the current connection-generation cache, not object-level identity constraints;
4. Reopening, active `get_device_info()`, and `recover()` use the same constraints;
5. An identity mismatch prevents Ready state and control-command admission.

## Main interfaces

```cpp
DiscoveryReport discover_hands(const DiscoveryConfig &config);

const DiscoveryCandidate &DiscoveryReport::require_one(bool allow_partial = false) const;

HandDeviceConfig DiscoveryCandidate::make_config() const;
HandDeviceConfig DiscoveryCandidate::make_config(const HandRuntimeConfig &runtime) const;

std::unique_ptr<HandDevice> DiscoveryCandidate::create_hand_device() const;
std::unique_ptr<HandDevice> DiscoveryCandidate::create_hand_device(const HandRuntimeConfig &runtime) const;
std::unique_ptr<HandDevice> DiscoveryCandidate::create_hand_device(const HandRuntimeConfig &runtime,
                                                                   HandSide side) const;
```

No-argument overloads retain default runtime policies; existing callers need no changes.

## Optional dependencies, drivers, and permissions

### Linux SocketCAN

- Requires kernel SocketCAN drivers and a CAN network interface already created and UP; typical deployments also
  use `iproute2` to configure interfaces outside the SDK.
- The application process must be allowed to open the corresponding CAN raw socket. Creating interfaces, setting bitrates, or bringing interfaces
  UP usually requires administrative capabilities, but discovery itself does not execute these system-configuration commands.
- Configure Classic CAN/CAN FD bitrates and FD enablement before calling the SDK.

### MiniCANFD

- Default runtime library names are `libcanbus.so` on Linux and `HCanbus.dll` on Windows; alternatively, specify a path through
  `DiscoveryConfig::minicanfd.library_path` or `LINKERBOT_CANFD_LIB`.
- Explicitly configure the device/channel layout on Linux; Windows can use default channel 0 for scanned devices. An incorrect
  layout produces `channel_layout_required` or enumeration failure.
- A missing dynamic library affects only the MiniCANFD source, not the core SDK or other backends.

### PCAN and SLCAN

The default PCAN source loads PCAN-Basic at runtime and uses `ATTACHED_CHANNELS_COUNT` / `ATTACHED_CHANNELS`
to enumerate attached channels, preserving native handles, hardware information, FD capability, and busy status. It does not blindly scan fixed channels.
`config.pcan.library_path` (a UTF-8 filesystem path) takes precedence over `LINKERBOT_PCAN_LIB`.
Missing libraries/symbols, unknown capabilities, and busy channels retain diagnostics. Classic-only or unknown FD capabilities do not initialize FD.

```cpp
DiscoveryConfig config;
config.backends = {"pcan"};
config.pcan.channels = {"PCAN_USBBUS2"}; // Use a channel actually returned by enumeration.
config.pcan.probe_protocols = {TransportKind::kCanFd};
config.pcan.nominal_bitrate = 1'000'000;
config.pcan.data_bitrate = 5'000'000;
config.pcan.fd_timing = linkerbot::can::PcanFdTiming{};
auto report = discover_hands(config);
```

Default FD timing is an engineering configuration that produces exactly 1/5 Mbit/s; verify it against the hardware.
Channel descriptions and connection keys retain configuration identifiers; factories capture the actual module, native channel,
protocol, bitrates, and timing used during enumeration. Destroying the source or changing caller configuration does not make candidates fall back to SocketCAN.
`create_hand_device()` stays closed with zero I/O; `open()` verifies identity again.

Constructing sources and manual factories does not load drivers; enumeration and probing occur in `discover_hands()`.
A failed backend preserves results from others, sharing the absolute deadline. The budget is checked again after enumeration/initialization returns;
identity probing does not begin after expiration. Synchronous vendor enumeration, initialization, and close APIs provide no cancellation argument,
so the SDK cannot interrupt a stuck driver. Total time may exceed `total_timeout`; actual driver bounds await Windows acceptance.

Existing PCAN/SLCAN enumerator and Transport-factory injection interfaces remain available.
Default SLCAN only queries serial metadata for `config.slcan.channels`, then probes exclusively. An empty allowlist
returns `allowlist_required`; common `config.channels` only filters results and cannot replace serial-access permission.
Default Lawicel Classic configuration accepts only `probe_protocols={TransportKind::kCan}`; FD/BRS are unsupported.
For CANable FD firmware, explicitly set `config.slcan.protocol=SLCANProtocol::kCanableFd`,
select Classic or CAN FD probing, and set actual nominal/data bitrates. The FD extension requires matching comm support with
`LINKERBOT_COMM_SLCAN_FD=1`. See [Adapter integration](../connections/windows-adapters.md) for protocol and configuration examples.
Older comm versions without a SLCAN backend return `backend_unavailable`.

```cpp
DiscoveryConfig config;
config.backends = {"slcan"};
config.slcan.channels = {"COM10"}; // Include only confirmed Lawicel Classic adapters.
config.slcan.serial_baud = 115200;
config.slcan.nominal_bitrate = 1'000'000;
config.slcan.command_timeout = std::chrono::milliseconds{500};
auto report = discover_hands(config);
```

Other COM ports are not scanned. Candidate factories capture this configuration and are unaffected by later changes to config.
For PCAN, SLCAN, and MiniCANFD read-only PowerShell steps,
complete FD timing, and injection for all six models, see [Windows adapter integration](../connections/windows-adapters.md).

## Failure diagnostics

Discovery reports can contain successful candidates and failures together. Log failures rather than checking candidate count alone:

```cpp
const auto report = discover_hands(config);
for (const auto &failure : report.failures()) {
    std::cerr << "backend=" << failure.backend().value_or("unknown") << " stage=" << failure.stage().value_or("unknown")
              << " channel=" << failure.native_channel().value_or("-") << " error=" << failure.message() << '\n';
}
```

Common error codes:

| Error code | Meaning / suggested action |
| --- | --- |
| `backend_not_found` | Requested backend is unregistered; check `backends` spelling and platform combinations |
| `backend_unavailable` | Optional adapter layer was not injected or driver is unavailable |
| `platform_backend_unavailable` | No default source for this platform; explicitly provide a custom source |
| `channel_not_found` | An explicitly requested channel was not enumerated |
| `unsupported_protocol` | Channel does not support the requested Classic CAN/CAN FD protocol |
| `timeout` | Global discovery budget exhausted; narrow the scope or adjust `total_timeout` |
| `backend_busy` | Channel is occupied; close other clients and retry |
| `transport_io` | Transport opening or I/O failed; inspect native status and driver logs |
| `channel_layout_required` | Backend such as MiniCANFD lacks an explicit device/channel layout |
| `backend_failed` | Driver loading, enumeration, or Transport initialization failed; inspect underlying status |
| `channel_descriptor_conflict` | Multiple sources describe the same physical channel inconsistently |
| `no_response` | Channel opens, but probe addresses return no identity response |

## Safety boundaries

- Discovery sends identity-read requests only. It does not call `enable()` or send angle, torque, current, parameter
  writes, or other motion-control commands.
- Discovery does not run system network commands such as `ip link` or create, start, stop, or modify SocketCAN
  interfaces. Deployment must configure SocketCAN bitrates beforehand.
- Initializing a temporary MiniCANFD Transport passes explicitly configured nominal/data bitrates to the
  vendor driver as required to open the adapter. Temporary Sessions and Transports close after scanning.
  Do not connect an unknown bus using mismatched bitrate settings.
- Creating sources, configurations, or `DiscoveryCandidate` objects performs no I/O. Only `discover_hands()`
  enumerates and sends identity queries; `create_hand_device()` still performs zero I/O.
- `HandDevice::open()` rereads physical identity, verifies the identity pinned by discovery, and establishes a safe
  baseline. Identity mismatch prevents Ready state and control-command admission.

For real-adapter acceptance steps and an evidence template, see
[Device discovery checks in hardware acceptance](../../hardware-acceptance/README.md#设备发现检查);
package users can find this checklist in the source repository at `docs/hardware-acceptance/README.md`.
