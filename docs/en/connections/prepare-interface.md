# Prepare the Communication Interface

This guide is for the C++ SDK. Build and install the SDK first, then prepare the communication interface for your operating system and adapter.

## Build and link prerequisites

You need a C++17 compiler, CMake 3.16+, and a `linkerbot-transport-cpp` version compatible with the hand SDK.
Hand and transport versions are independent. Hand 0.3.12 requires transport `0.3.x` at version 0.3.4 or newer.
For build and installation steps, see the [Project quick start](../../../README.md); for Windows, see the [Windows guide](../guides/windows.md).

Save a complete program from this directory as your application's `main.cpp`. After installing the SDK, use this minimal `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(hand_demo LANGUAGES CXX)
find_package(linkerbot-hand-cpp 0.3 CONFIG REQUIRED)
add_executable(hand_demo main.cpp)
target_compile_features(hand_demo PRIVATE cxx_std_17)
target_link_libraries(hand_demo PRIVATE linkerbot::cpp-sdk)
if(MSVC)
    target_compile_options(hand_demo PRIVATE /utf-8)
endif()
```

Place `CMakeLists.txt` and `main.cpp` in your application directory and build from there (replace `/path/to/linkerbot-hand-cpp` with your extracted SDK package directory or install prefix; omit `-DCMAKE_PREFIX_PATH` if installed into a standard system prefix such as `/usr/local`):

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/linkerbot-hand-cpp"
cmake --build build --config Release
```

`HandDevice` does not directly accept Python transport arguments or model-name strings. In C++, use
`HandDeviceConfig`, `Model`, and `HandSide`. For non-SocketCAN connections, bind the adapter through a factory or discovery candidate.

## Linux: SocketCAN

The driver or firmware must first expose a network interface such as `can0`; then configure its bitrate. A serial port name such as `/dev/ttyACM0`
does not mean SocketCAN is available. Replace `can0` in the following commands with your actual interface.
Automatic discovery does not run `ip link` or change the interface bitrate for you.

### Classic CAN

L6, O6, L20 Lite, and L20 with Classic firmware—as well as O6 HOP running in CAN 2.0 mode (`O6Protocol::kHopV004Can`)—use 1 Mbit/s:

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 fd off
sudo ip link set can0 up
ip -details link show can0
```

### CAN FD

L30, O30, and O6 HOP running in CAN FD mode (`O6Protocol::kHopV004`) use a nominal bitrate of 1 Mbit/s and a data bitrate of 5 Mbit/s:

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can0 up
ip -details link show can0
```

L30 uses extended FD frames; O30 and O6 HOP use standard FD frames without BRS. The model and protocol configuration determine the frame format.
L6 and O6 still default to Classic CAN. Explicitly select an optional HOP protocol only when the device firmware matches;
setting the system interface to FD alone does not switch the device protocol. See [Optional L6 protocol](../guides/usage.md#optional-l6-can-fd-protocol) and [Manual O6 HOP connection and mode switching](../tutorials/o6.md#o6-hop).

## Windows and standalone adapter backends

These backends do not require the Linux `ip link` commands.

| Backend | Prerequisites and channel configuration |
| --- | --- |
| PCAN | Install the vendor driver and a PCAN-Basic dynamic library matching the application architecture; channel names include `PCAN_USBBUS1`. FD requires FD-capable hardware and bit timing. |
| SLCAN | Confirm the actual COM port, serial baud rate, and firmware protocol; explicitly distinguish Classic Lawicel from CANable FD. |
| MiniCANFD | Use `libcanbus.so` on Linux and `HCanbus.dll` on Windows. Match the library to the application architecture and specify the actual device/channel. |

The serial baud rate `serial_baud` and the CAN bitrate are different parameters. The CANable FD data bitrate must match the firmware and device.
MiniCANFD discovery on Linux requires confirmed device/channel pairs; it does not automatically fall back from channel 0 to channel 1.
For MiniCANFD on Windows, `device_index` is the channel number enumerated by the vendor, and `channel_index` is fixed at 0;
on Linux, they are the device number and the channel number within that device, respectively.

For driver installation, dynamic-library paths, and validated configurations, see [Adapter integration](windows-adapters.md) and
[Platform and hardware validation scope](../reference/compatibility.md). An available API does not mean every hardware combination has been tested.

<a id="checks"></a>

## Next steps

- Model, address, or channel unknown: read [Automatic connection](automatic.md).
- Deployment configuration known: read [Manual connection](manual.md).

[Back to the documentation overview](../README.md)
