# Windows 使用指南

本文说明 Windows 下的构建、安装和 C++ 项目接入。首次使用先完成构建，再按
[适配器接入指南](../connections/windows-adapters.md)读取设备。PCAN、MiniCANFD、SLCAN 的驱动与连接参数统一在该文档维护。
实测范围见 [平台与硬件验证范围](../reference/compatibility.md#平台与硬件验证范围)。

## 环境要求

- Windows x64，Visual Studio 2022，安装“使用 C++ 的桌面开发”和 Windows SDK。
- Git、CMake 3.21+（支持 VS 2022 生成器）；在 **x64 Developer PowerShell for VS 2022** 中执行下文命令。
- PCAN、MiniCANFD 使用匹配的 x64 驱动和 DLL；SLCAN 使用串口驱动及兼容固件。
- SDK、应用和依赖统一使用 Release 或 Debug。Release 使用 `/MD`，Debug 使用 `/MDd`；
  Release 部署需要对应的 VC++ x64 运行库，Debug 运行库用于开发环境。

## 构建 SDK

两个源码仓库放在同一级目录，transport 提交与 README、CI 保持一致：

```powershell
git clone https://github.com/linker-bot/linkerbot-transport-cpp.git
git -C linkerbot-transport-cpp checkout --detach de87774d1b23d0a1fa7aaaaacb7f50ea7ffb3008
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

构建不需要厂商开发包或 Catch2。`--help` 不加载驱动、不访问硬件。
实际枚举和只读连接按[适配器接入步骤](../connections/windows-adapters.md#2-选择适配器并读取)执行。
Windows 不使用 `can0` 或 Linux 的 `ip link` 命令。

## 本地运行 Windows CI

另外安装 Windows Python 3.9+，在仓库根目录的普通 PowerShell 中执行：

```powershell
# 日常快速验证：编译一次，运行离线和 DLL 加载两组测试，不安装或打包。
./validation/run_windows_ci.ps1 -Quick
# 完整安装与发布包验收：
./validation/run_windows_ci.ps1
# 同时检查 Debug 和 Release：
./validation/run_windows_ci.ps1 -Configuration Both
# 使用指定的通信库源码：
./validation/run_windows_ci.ps1 -CommSource ../linkerbot-transport-cpp -Suite no-drivers
```

脚本自动寻找 VS 2022 和 CMake，默认运行 Release 的全部 Windows 验证，包含
通信库与 SDK 离线测试、模拟 DLL 加载测试、安装消费、文档示例编译及 ZIP 打包审计。
不需要手或厂商驱动；首次运行会下载 Catch2。日志与结果保存在
`build/wci-*/<配置>/evidence/`，每次使用新目录，可验证未提交的本地修改。
GitHub PR 使用快速验证；推送到主分支及发布工作流保留完整验收。Windows 每个配置
只构建一次，然后运行两组测试。若 `python` 指向商店别名，可通过 `-PythonExecutable`
指定实际的 Python 路径。
复现 CI 时请使用本文指定的 transport 提交；本地脚本使用已安装的 v143 工具集，
远程 CI 则固定 CMake 3.31.8 和 MSVC 14.44。Linux GCC 与 ASan/UBSan 任务需要 Linux
环境（例如 WSL2），不能直接由此 Windows 脚本执行。

## 安装与应用接入

在 SDK 源码根目录安装到自己的开发目录：

```powershell
cmake --install build-windows --config Release --prefix C:/Work/linkerbot-sdk
if ($LASTEXITCODE -ne 0) { throw 'Install failed' }
```

在单独的应用目录创建 `main.cpp`。下面通过 PCAN Classic 读取 L20 右手，按实际设备修改
DLL 路径、通道、型号、侧别和地址；示例不发送运动目标：

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

在同一目录创建 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
project(hand_demo LANGUAGES CXX)
find_package(linkerbot-hand-cpp 0.3 CONFIG REQUIRED)
add_executable(hand_demo main.cpp)
target_compile_features(hand_demo PRIVATE cxx_std_17)
target_link_libraries(hand_demo PRIVATE linkerbot::cpp-sdk)
```

在应用目录构建并运行：

```powershell
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Work/linkerbot-sdk
if ($LASTEXITCODE -ne 0) { throw 'Consumer configure failed' }
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Consumer build failed' }
& .\build\Release\hand_demo.exe
```

成功时输出产品码和 16 个原始角度。改用 MiniCANFD 或 SLCAN 时替换 factory 配置，见
[适配器接入指南](../connections/windows-adapters.md#3-在-c-程序中连接)。
直接使用 `HandDevice(config)` 的默认 transport 为 Linux SocketCAN；Windows 需要适配器 factory 或共享 Session。
连接后的控制、轮询及错误处理见 [API 使用指南](usage.md)。

## 测试与配置

首次构建关闭了测试。需要离线测试时，先安装与 SDK 使用相同工具集和运行库配置的 Catch2 3，
再在 SDK 源码根目录执行：

```powershell
cmake -S . -B build-windows -DLINKERBOT_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'Test configure failed' }
cmake --build build-windows --config Release --parallel 4 --target tests protocol-tests
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
ctest --test-dir build-windows -C Release --output-on-failure
```

Catch2 不在默认搜索路径时添加 `-DCatch2_DIR=C:/path/to/Catch2/lib/cmake/Catch2`。
真实设备验证另见 [Windows 真机验收步骤](../../hardware-acceptance/windows.md)。

默认角度映射文件保存在 `%LOCALAPPDATA%\linkerbot\hand_angle_mappings.toml`，也可显式设置
`config.angle_mapping_path`，目录优先级见[配置目录说明](../connections/windows-adapters.md#角度映射配置目录)。
应用被系统策略阻止运行时，先解决运行权限；DLL 加载、端口占用等问题见[适配器排错](../connections/windows-adapters.md#常见问题)。
