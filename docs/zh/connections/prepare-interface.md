# 准备通信接口

本文适用于 C++ SDK。先安装并构建 SDK，再按操作系统和适配器准备通信接口。

## 编译与链接前提

需要 C++17 编译器、CMake 3.16+，以及与 hand SDK 配套的 `linkerbot-transport-cpp`。
hand 与 transport 使用兼容的 `0.3.x` 版本，transport 版本不得低于 hand。
构建和安装步骤见[项目快速开始](../../../README.md)；Windows 见[Windows 使用指南](../guides/windows.md)。

本目录的完整程序可保存为应用的 `main.cpp`。SDK 安装后，最小 `CMakeLists.txt` 为：

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

将应用文件放在 `demo/`，从 SDK 源码根目录执行；`install` 替换为实际 SDK 安装前缀：

```bash
cmake -S demo -B demo/build -DCMAKE_PREFIX_PATH="$PWD/install"
cmake --build demo/build --config Release
```

`HandDevice` 不直接接收 Python 的 transport 参数或字符串型号，C++ 使用
`HandDeviceConfig`、`Model`、`HandSide`，非 SocketCAN 连接通过 factory 或发现候选绑定适配器。

## Linux：SocketCAN

系统必须先由驱动或固件提供 `can0` 等网络接口，再配置速率。出现 `/dev/ttyACM0`
等串口名称不等于已具备 SocketCAN。下面命令中的 `can0` 应替换为实际接口。
自动发现不会替你执行 `ip link`，也不会修改接口速率。

### 经典 CAN

Classic 固件 L6、O6、L20 Lite、L20 使用 1 Mbit/s：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 fd off
sudo ip link set can0 up
ip -details link show can0
```

### CAN FD

L30、O30 使用仲裁速率 1 Mbit/s、数据速率 5 Mbit/s：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can0 up
ip -details link show can0
```

L30 使用扩展 FD 帧，O30 使用标准 FD 帧、无 BRS；帧格式由型号实现决定。
L6 默认仍是 Classic CAN。只有设备固件匹配时才显式选择可选 HOP CAN FD 协议；
仅将系统接口设成 FD 不会切换 L6 协议，详见[可选协议](../guides/usage.md#l6-可选-can-fd-协议)。

## Windows 与独立适配器后端

这些后端不需要执行 Linux 的 `ip link` 命令。

| 后端 | 前提与通道配置 |
| --- | --- |
| PCAN | 安装厂商驱动与匹配应用架构的 PCAN-Basic 动态库；通道名如 `PCAN_USBBUS1`，FD 需要支持 FD 的硬件与位时序 |
| SLCAN | 确认实际 COM 口、串口速率及固件协议；Classic Lawicel 和 CANable FD 必须显式区分 |
| MiniCANFD | Linux 使用 `libcanbus.so`，Windows 使用 `HCanbus.dll`；库与应用架构一致，指定实际设备/通道 |

串口速率 `serial_baud` 和 CAN 速率不是同一个参数。CANable FD 的数据速率需要与固件和设备匹配。
Linux MiniCANFD 的发现必须提供已确认的设备/通道组合，不会自动从通道 0 回退到 1。
Windows 的 MiniCANFD `device_index` 表示厂商枚举的通道编号，`channel_index` 固定为 0；
Linux 两者分别表示设备编号和设备内通道编号。

驱动安装、动态库路径和已验收范围见[适配器接入](windows-adapters.md)与
[平台和硬件验证范围](../reference/compatibility.md)。可调用接口并不代表所有硬件组合都已完成实测。

<a id="checks"></a>

## 下一步

- 型号、地址或通道未知：阅读[自动连接](automatic.md)。
- 已知部署配置：阅读[手动连接](manual.md)。

[返回文档总览](../README.md)
