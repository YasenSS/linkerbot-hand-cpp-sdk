# 自动连接

<a id="automatic"></a>

设备发现先读取身份，再由候选创建 `HandDevice`。发现过程不发送角度、力矩、使能等控制命令。
Linux SocketCAN 请先[准备通信接口](prepare-interface.md)。

| 操作系统 | 默认后端 | 需要额外确认的内容 |
| --- | --- | --- |
| Linux | SocketCAN、MiniCANFD | SocketCAN 接口已启用；MiniCANFD 需要显式设备/通道组合 |
| Windows | PCAN、SLCAN、MiniCANFD | PCAN/MiniCANFD 驱动和库；SLCAN 必须填写串口允许列表 |

各后端共用发现总预算；一个后端失败不会丢弃其他后端的成功候选。
L6 可选 HOP 协议不参与当前自动发现，需[手动配置](manual.md#socketcan)。

## 连接设备

### 自动发现一个设备

以下是完整的只读程序。`require_one()` 要求全部候选中恰好只有一个，且身份完整；
没有候选、多个候选或仅有部分身份时会抛出不同异常。

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

// 无论读取成功还是异常，都尝试关闭同一个设备对象，并保留原始异常。
void read_and_close(HandDevice &hand) {
    std::exception_ptr failure;
    try {
        hand.open();
        const auto angles = hand.get_angles(1s);
        if (!angles.meta.valid) {
            throw std::runtime_error("角度样本不可用");
        }
        if (angles.meta.stale) {
            std::cerr << "角度样本已超过新鲜度窗口\n";
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
        std::cerr << "关闭连接失败：" << error.what() << '\n';
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
    std::cout << "通道：" << candidate.channel().name() << '\n';
    // 返回 unique_ptr；构造时不打开通道，read_and_close() 内 open() 才建立连接。
    auto hand = candidate.create_hand_device();
    read_and_close(*hand);
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

对象方法使用 `hand->...`；传递引用时使用 `*hand`。`create_hand_device()` 不打开连接，
`open()` 会再次校验身份、建立基线。只读示例可直接运行；添加运动操作前先确认设备和任务范围。

### 自动发现多个设备

下面是独立完整程序。C++ 的 `verified_candidates()` 返回候选引用包装列表，通过 `.get()` 访问。
示例逐台连接、读取、关闭；共享总线的并发使用方式见[设备发现说明](../reference/discovery.md)。

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

// 无论读取成功还是异常，都尝试关闭同一个设备对象，并保留原始异常。
void read_and_close(HandDevice &hand) {
    std::exception_ptr failure;
    try {
        hand.open();
        const auto angles = hand.get_angles(1s);
        if (!angles.meta.valid) {
            throw std::runtime_error("角度样本不可用");
        }
        if (angles.meta.stale) {
            std::cerr << "角度样本已超过新鲜度窗口\n";
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
        std::cerr << "关闭连接失败：" << error.what() << '\n';
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
        throw std::runtime_error("没有身份完整的设备，请检查报告中的失败和 partial 候选");
    }
    // 顺序连接和读取；某台失败后继续其他设备，并以退出码报告失败。
    bool failed = false;
    for (const auto &reference : candidates) {
        const auto &candidate = reference.get();
        std::cout << "通道：" << candidate.channel().name() << '\n';
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

## 限定后端和通道

下列配置片段替换完整程序中的 `DiscoveryConfig scan;`，其后的 `discover_hands(scan)` 等步骤不变。
型号和地址由发现结果确定，不用手写字符串型号。

### SocketCAN（Linux）

```cpp
DiscoveryConfig scan;
scan.backends = {"socketcan"};
scan.channels = {"can0"};
scan.total_timeout = 10s;
```

### PCAN（Windows）

默认枚举可用 PCAN Classic 通道。限定通道并指定动态库可用：

```cpp
DiscoveryConfig scan;
scan.backends = {"pcan"};
scan.pcan.channels = {"PCAN_USBBUS1"};
scan.pcan.library_path = "C:/vendor/PCANBasic.dll";
scan.pcan.nominal_bitrate = 1'000'000;
```

探测 FD 时需要改用 `probe_protocols = {TransportKind::kCanFd}`，并按硬件填写
`data_bitrate` 和 `fd_timing`。PCAN FD 具有代码支持，但应核对[实测范围](../reference/compatibility.md)。

### SLCAN（Windows）

<a id="slcan"></a>

C++ 不按 Python 的 VID/PID 规则自动访问串口。必须在 `scan.slcan.channels` 指定实际 COM 口；
公共 `scan.channels` 仅过滤结果，不能代替允许列表。未填写时返回 `allowlist_required`。
下面以 CANable FD 固件、1M/5M 总线为例：

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

Classic Lawicel 固件使用 `SLCANProtocol::kLawicelClassic` 和
`probe_protocols = {TransportKind::kCan}`；此组合不支持 FD。
CANable FD 扩展需要配套 transport 库，不能仅修改后端名称就视为协议兼容。

### MiniCANFD

<a id="minicanfd"></a>

Linux 必须填写已确认的 `(device_index, channel_index)`，无配置时可能返回
`channel_layout_required`。不自动扫描数量或从通道 0 回退到 1；需要检查两个通道时显式列出两项。

```cpp
DiscoveryConfig scan;
scan.backends = {"minicanfd"};
scan.minicanfd.library_path = "/opt/minicanfd/libcanbus.so";
scan.minicanfd.channels = {{0, 0}, {0, 1}};
scan.minicanfd.nominal_bitrate = 1'000'000;
scan.minicanfd.data_bitrate = 5'000'000;
```

Windows 可省略 `channels`，由厂商库枚举可用通道；显式配置时第一项是厂商通道编号，
第二项固定为 `0`，如 `{{0, 0}, {1, 0}}`。库路径改为实际 `HCanbus.dll` 路径。
发现结果绑定了实际模块和配置，后续打开不会退回 SocketCAN。

## 身份不完整与诊断

`report.candidates()` 包含完整与部分候选，`partial_candidates()` 可用于单独展示部分身份。
只有问题全部为 `missing_side` 时，才能在人工确认左右手后补全；不能忽略型号或地址冲突。
以下片段也是替换完整程序中创建 `hand` 的部分，`report` 必须来自本次发现：

```cpp
DiscoveryConfig scan;
const auto report = discover_hands(scan);
const auto &candidate = report.require_one(true);
HandRuntimeConfig runtime;
runtime.identity_timeout = 2s;
auto hand = candidate.create_hand_device(runtime, HandSide::kLeft);
// 继续调用 read_and_close(*hand)；左右手必须来自已确认的部署信息。
```

请同时检查 `report.failures()` 的 `code()`、`message()`、`backend()` 和 `native_channel()`。
常见原因包括无回复、接口未启用、通道占用、动态库缺失、串口允许列表缺失或发现预算耗尽。
完整错误码说明见[设备发现诊断](../reference/discovery.md#失败诊断)。

关闭失败时应保留同一个设备对象，在排除驱动或连接故障后重试 `close()`；
不要把清理错误当成已关闭，也不要立即另开同一设备。上面的命令行程序记录关闭错误并退出，
长期运行的应用应由自己的连接管理器保留对象并安排清理重试。

连接后使用[各型号教程](../tutorials/l6.md)，或按型号选择
[O6](../tutorials/o6.md)、[L20 Lite](../tutorials/l20lite.md)、[L20](../tutorials/l20.md)、
[L30](../tutorials/l30.md)、[O30](../tutorials/o30.md)。

[返回文档总览](../README.md)
