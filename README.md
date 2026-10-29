# LinkerBot 灵巧手 C++ SDK

[简体中文](README.md) | [English](README.en.md)

面向 LinkerBot 灵巧手的 C++17 SDK，提供设备连接、关节控制、传感器读取和后台轮询。
底层 CAN/CAN FD 通信由配套的 [linkerbot-transport-cpp](https://github.com/linker-bot/linkerbot-transport-cpp) 提供。

- **Linux 用户**：按下方快速开始，使用 SocketCAN 连接设备。
- **Windows 用户**：从 [Windows 使用指南](docs/windows.md)开始，按适配器选择连接方式。
- **已有项目**：参阅 [API 使用指南](docs/usage.md)和[设备发现](docs/hand-device-discovery.md)。

## 支持的型号

| 型号 | 关节数 | 默认协议 | 地址 |
| --- | ---: | --- | --- |
| L6 | 6 | Classic CAN | 左手 `0x28`，右手 `0x27` |
| O6 | 6 | Classic CAN | 左手 `0x28`，右手 `0x27` |
| L20 Lite | 10 | Classic CAN | 左手 `0x28`，右手 `0x27` |
| L20 | 16 | Classic CAN | 左手 `0x28`，右手 `0x27` |
| L30 | 17 | CAN FD 扩展帧 | NodeID，默认 `1`；HostID 默认 `0` |
| O30 | 20 | HOP / CAN FD 标准帧，无 BRS | 请求 ID 默认 `0x001` |

L6 默认使用 Classic CAN；支持 CAN FD 的设备可显式选择[可选 HOP 协议](docs/usage.md#l6-可选-can-fd-协议)。
平台、适配器和实测限制统一见
[平台与硬件验证范围](docs/COMPATIBILITY.md#平台与硬件验证范围)。

## 快速开始（Linux）

以下以 **Ubuntu、SocketCAN、Classic 固件的 L6 左手**为例，自动发现设备并建立只读连接。
需要 Git、CMake 3.16+、C++17 编译器；验证基线为 Ubuntu 24.04 / GCC 13。

### 1. 获取并构建

两个仓库放在同一级目录。transport 使用与当前 CI 一致的提交；两库须配套使用
`0.3.x`，transport 版本不得低于 hand 版本。

```bash
mkdir sdk-build && cd sdk-build
git clone https://github.com/linker-bot/linkerbot-transport-cpp.git
git -C linkerbot-transport-cpp checkout --detach 37c2e6f05ec93045e13459194b6bee7d1a2f091d
git clone https://github.com/linker-bot/linkerbot-hand-cpp.git
cd linkerbot-hand-cpp

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DLINKERBOT_COMM_SOURCE_DIR=../linkerbot-transport-cpp \
  -DLINKERBOT_BUILD_TESTS=OFF -DLINKERBOT_BUILD_TOOLS=OFF \
  -DLINKERBOT_BUILD_EXAMPLES=OFF -DLINKERBOT_COMM_BUILD_TESTS=OFF \
  -DLINKERBOT_COMM_BUILD_EXAMPLES=OFF -DLINKERBOT_ALLOW_DEPENDENCY_DOWNLOADS=OFF
cmake --build build --parallel 4
cmake --install build --prefix "$PWD/install"
```

这会将 SDK 和配套通信库安装到当前目录的 `install/`，不需要 Catch2 或厂商开发包。
后续命令均从 `linkerbot-hand-cpp` 源码根目录执行。

### 2. 连接 CAN 总线

连接适配器、供电和 CAN 接线，将接口名及速率改为实际值：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 fd off
sudo ip link set can0 up
ip -details link show can0
```

接口应显示为 `UP`、速率为 `1000000`。自动发现使用已启用的接口，不会修改系统 CAN 配置。
其他型号的 CAN FD 配置见
[连接 CAN 总线](docs/usage.md#连接-can-总线)。

### 3. 创建只读应用

执行 `mkdir -p demo`，将下面代码保存为 `demo/main.cpp`。SDK 扫描 SocketCAN 通道，
自动识别设备型号、左右手和地址，再创建 `HandDevice`；无需在代码中手动指定这些信息。
示例要求发现结果中只有一台设备且身份完整，只读取身份和角度，不发送运动目标。

```cpp
#include <exception>
#include <iostream>
#include <linkerbot/hand/discovery.hpp>
#include <stdexcept>

int main() try {
    using namespace linkerbot::hand;
    DiscoveryConfig scan;
    scan.backends = {"socketcan"};
    const auto report = discover_hands(scan);
    const auto &candidate = report.require_one();
    std::cout << "channel=" << candidate.channel().name() << '\n';
    auto hand = candidate.create_hand_device();
    hand->open();
    const auto identity = hand->get_device_info();
    const auto angles = hand->get_angles();
    if (!angles.meta.valid) {
        throw std::runtime_error("Invalid angle response");
    }
    std::cout << "product=" << identity.product_code.value_or("unreported") << '\n';
    std::cout << "angles:";
    for (const auto value : angles.raw) {
        std::cout << ' ' << value;
    }
    std::cout << '\n';
    hand->close();
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

将下面内容保存为 `demo/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
project(hand_demo LANGUAGES CXX)
find_package(linkerbot-hand-cpp 0.3 CONFIG REQUIRED)
add_executable(hand_demo main.cpp)
target_compile_features(hand_demo PRIVATE cxx_std_17)
target_link_libraries(hand_demo PRIVATE linkerbot::cpp-sdk)
```

### 4. 编译并运行

```bash
cmake -S demo -B demo/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/install"
cmake --build demo/build --parallel 4
./demo/build/hand_demo
```

成功时输出发现的通道、设备产品码和原始角度（L6 为六个值），随后关闭连接，退出码为 `0`。
未发现设备时检查接口状态、供电、速率和占用情况；多设备选择、身份缺少左右手等情况见
[设备发现说明](docs/hand-device-discovery.md)。已知固定设备的[手动配置](docs/usage.md#读取与控制)放在使用指南中。

## 后续使用

新项目推荐使用统一的 `HandDevice`；需要型号专用功能时使用 `l6::L6` 等型号类。
命令提交、完成判断、轮询和错误恢复见 [API 使用指南](docs/usage.md)。
统一 setter 返回 `CommandHandle`；返回表示命令已接纳，执行结果需要检查 handle。

需要离线测试时，在 SDK 源码根目录安装 Catch2 3，重新配置独立测试目录：

```bash
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release \
  -DLINKERBOT_BUILD_TESTS=ON -DLINKERBOT_BUILD_TOOLS=OFF \
  -DLINKERBOT_ALLOW_DEPENDENCY_DOWNLOADS=OFF -DLINKERBOT_COMM_BUILD_TESTS=OFF
cmake --build build-tests --parallel 4
ctest --test-dir build-tests --output-on-failure
```

硬件测试的配置与执行步骤见[实机测试说明](tests/hand/device/hardware/README.md)。

## 文档

- [Windows 使用指南](docs/windows.md)：环境、构建、安装和应用接入。
- [Windows 适配器接入](docs/windows-adapters.md)：PCAN、MiniCANFD、SLCAN 命令和配置。
- [API 使用指南](docs/usage.md)：读取、控制、后台轮询和生命周期。
- [各型号能力说明](docs/model-capabilities.md)：功能总览、读写入口、数值范围及固件限制。
- [设备发现](docs/hand-device-discovery.md)：自动发现设备及配置职责。
- [平台与硬件验证范围](docs/COMPATIBILITY.md)：实际验证过的环境与通信路径。
- [版本变更](CHANGELOG.md) · [贡献说明](CONTRIBUTING.md) · [文档索引](docs/README.md)。

项目采用 [MIT 许可证](LICENSE)，第三方依赖说明见 [NOTICE](NOTICE)。
