# Windows Guide

This guide covers building, installing, and integrating the SDK into C++ projects on Windows. Build the SDK first, then follow the
[Adapter integration guide](../connections/windows-adapters.md) to read a device. Driver requirements and connection parameters for PCAN, MiniCANFD, and SLCAN are maintained there.
For tested configurations, see [Platform and hardware validation scope](../reference/compatibility.md#platform-and-hardware-validation-scope).

## Environment requirements

- Windows x64 and Visual Studio 2022 with the "Desktop development with C++" workload and Windows SDK installed.
- Git and CMake 3.21+ (for the VS 2022 generator). Run the commands below in **x64 Developer PowerShell for VS 2022**.
- Matching x64 drivers and DLLs for PCAN/MiniCANFD; a serial driver and compatible firmware for SLCAN.
- Use the same Release or Debug configuration for the SDK, application, and dependencies. Release uses `/MD`, and Debug uses `/MDd`;
  Release deployment requires the corresponding VC++ x64 runtime, while the Debug runtime is for development environments.

## Building the SDK

Place the two source repositories side by side. Use the same transport commit as the README and CI:

```powershell
git clone https://github.com/linker-bot/linkerbot-transport-cpp.git
git -C linkerbot-transport-cpp checkout --detach a46177e86e403bbd18834aedb6e81ef3233c6c22
git clone https://github.com/linker-bot/linkerbot-hand-cpp-sdk.git linkerbot-hand-cpp
Set-Location linkerbot-hand-cpp

cmake -S . -B build-windows -G 'Visual Studio 17 2022' -A x64 `
  -DLINKERBOT_COMM_SOURCE_DIR=../linkerbot-transport-cpp `
  -DLINKERBOT_BUILD_TESTS=OFF -DLINKERBOT_BUILD_TOOLS=OFF `
  -DLINKERBOT_BUILD_EXAMPLES=ON -DLINKERBOT_COMM_BUILD_TESTS=OFF `
  -DLINKERBOT_COMM_BUILD_EXAMPLES=OFF -DLINKERBOT_ALLOW_DEPENDENCY_DOWNLOADS=OFF
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build build-windows --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& .\build-windows\examples\Release\windows_readonly.exe --help
```

Building requires neither a vendor development package nor Catch2. `--help` does not load drivers or access hardware.
For actual enumeration and read-only connections, follow the [Adapter integration steps](../connections/windows-adapters.md#2-select-an-adapter-and-read).
Windows does not use `can0` or the Linux `ip link` commands.

## Running Windows CI locally

Install Windows Python 3.9+ in addition to the build tools, then run from the
repository root in a regular PowerShell:

```powershell
# Daily checks: build once, run offline and DLL loader tests, skip install/package.
./validation/run_windows_ci.ps1 -Quick
# Full installation and release-package acceptance:
./validation/run_windows_ci.ps1
# Check both Debug and Release:
./validation/run_windows_ci.ps1 -Configuration Both
# Select a transport source checkout and test suite:
./validation/run_windows_ci.ps1 -CommSource ../linkerbot-transport-cpp -Suite no-drivers
```

The script locates VS 2022 and CMake automatically. By default it runs all Windows
Release checks: transport and SDK offline tests, fixture DLL loading, installed
consumers, documented examples, and ZIP package audits. No hardware or vendor
drivers are needed. The first run downloads Catch2. Each run uses a fresh directory
under `build/wci-*/<configuration>/evidence/` and can test uncommitted
changes. GitHub PRs use quick checks; pushes to the main branch and release workflows
retain full acceptance. Each Windows configuration builds once before running both
test suites. Use `-PythonExecutable` to select an interpreter if `python` resolves
to a Store alias. To reproduce CI, use the transport commit specified above. Local runs use
the installed v143 toolset; remote CI pins CMake 3.31.8 and MSVC 14.44. Linux GCC
and ASan/UBSan jobs require Linux (for example WSL2) and are not run by this script.

## Installation and application integration

From the SDK source root, install into your own development directory:

```powershell
cmake --install build-windows --config Release --prefix C:/Work/linkerbot-sdk
if ($LASTEXITCODE -ne 0) { throw 'Install failed' }
```

Create `main.cpp` in a separate application directory. The following reads a right-hand L20 through PCAN Classic; adjust the
DLL path, channel, model, hand side, and address for your device. The example sends no motion targets:

```cpp
#include <chrono>
#include <exception>
#include <iostream>
#include <linkerbot/hand/adapter_transport.hpp>

int main() try {
    namespace can = linkerbot::can;
    namespace hand = linkerbot::hand;
    using namespace std::chrono_literals;

    can::PcanConfig pcan;
    pcan.channel = can::pcan_channel_handle("PCAN_USBBUS1");
    pcan.mode = can::PcanMode::kClassic;
    pcan.nominal_bitrate = 1'000'000;
    pcan.library_path = "C:/Windows/System32/PCANBasic.dll";

    hand::HandDeviceConfig config;
    config.model = hand::Model::kL20; // L6 uses kL6; match the actual hand.
    config.side = hand::HandSide::kRight;
    config.address = 0x27;
    config.interface_name = "pcan:PCAN_USBBUS1";

    hand::HandDevice device(config, hand::make_pcan_transport_factory(pcan));
    device.open();
    const auto identity = device.get_device_info(1s);
    const auto angles = device.get_angles(500ms);
    std::cout << "product=" << identity.product_code.value_or("unreported") << '\n';
    for (const auto value : angles.raw) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
    device.close();
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

Create `CMakeLists.txt` in the same directory:

```cmake
cmake_minimum_required(VERSION 3.16)
project(hand_demo LANGUAGES CXX)
find_package(linkerbot-hand-cpp 0.3 CONFIG REQUIRED)
add_executable(hand_demo main.cpp)
target_compile_features(hand_demo PRIVATE cxx_std_17)
target_link_libraries(hand_demo PRIVATE linkerbot::cpp-sdk)
```

Build and run from the application directory:

```powershell
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Work/linkerbot-sdk
if ($LASTEXITCODE -ne 0) { throw 'Consumer configure failed' }
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Consumer build failed' }
& .\build\Release\hand_demo.exe
```

On success, the program prints the product code and 16 raw angles. For MiniCANFD or SLCAN, replace the factory configuration; see the
[Adapter integration guide](../connections/windows-adapters.md#3-connect-in-a-c-program).
The default transport for `HandDevice(config)` is Linux SocketCAN; Windows requires an adapter factory or shared Session.
For control, polling, and error handling after connecting, see the [API guide](usage.md).

## Testing and configuration

Tests are disabled in the initial build. To run offline tests, first install Catch2 3 using the same toolset and runtime configuration as the SDK,
then run the following from the SDK source root:

```powershell
cmake -S . -B build-windows -DLINKERBOT_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'Test configure failed' }
cmake --build build-windows --config Release --parallel 4 --target tests protocol-tests
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
ctest --test-dir build-windows -C Release --output-on-failure
```

If Catch2 is outside the default search paths, add `-DCatch2_DIR=C:/path/to/Catch2/lib/cmake/Catch2`.
For real-device validation, see [Windows hardware acceptance](../../hardware-acceptance/windows.md).

By default, angle mappings are stored in `%LOCALAPPDATA%\linkerbot\hand_angle_mappings.toml`. You may also explicitly set
`config.angle_mapping_path`; see [Configuration directory](../connections/windows-adapters.md#angle-mapping-configuration-directory) for directory precedence.
If system policy blocks the application, resolve execution permissions first. For DLL loading and busy ports, see [Troubleshooting](../connections/windows-adapters.md#troubleshooting).
