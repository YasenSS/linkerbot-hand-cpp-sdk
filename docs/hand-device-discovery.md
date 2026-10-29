# HandDevice discovery 公开接口

本文说明 C++ SDK 中设备扫描、运行策略和最终设备配置之间的边界，以及发现结果
如何安全地创建 `HandDevice`。

## 平台默认组合

| 平台 | 默认 backend | 说明 |
| --- | --- | --- |
| Linux | SocketCAN、MiniCANFD | SocketCAN 使用系统接口；MiniCANFD 运行时加载厂商动态库 |
| Windows | PCAN、SLCAN、MiniCANFD | PCAN/MiniCANFD 使用原生适配层；SLCAN 支持 Lawicel Classic 和显式 CANable FD 配置，需配套支持该后端的 comm |


默认入口按表中顺序组合 backend，并共享同一个 `total_timeout`。单个 backend、通道
或适配器失败不会丢弃其他 backend 已经发现的候选。
L6 的可选 HOP CAN FD 协议当前不参与自动发现，需按[可选协议说明](usage.md#l6-可选-can-fd-协议)显式配置连接。

## 选择 backend、通道和自定义 source

默认扫描：

```cpp
DiscoveryConfig config;
const auto report = discover_hands(config);
```

只扫描 Linux SocketCAN 的 `can0`：

```cpp
DiscoveryConfig config;
config.backends = {"socketcan"};
config.channels = {"can0"};
const auto report = discover_hands(config);
```

通道过滤既接受统一通道名，也接受 backend 原生通道名。显式请求的 backend 或通道
不存在时会形成 failure，而不是静默返回空结果。

显式组合 source：

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

PCAN/SLCAN 平台集成或测试代码可以向对应 source 注入
`AdapterChannelEnumerator` 和 `AdapterTransportFactory`。source 与配置对象的构造
不会枚举设备、加载驱动或打开通道。

## 配置职责

| 类型 | 所有者 | 内容 |
| --- | --- | --- |
| `DiscoveryConfig` | 扫描调用方 | 通道过滤、发现超时、探测地址范围和路由队列容量 |
| `HandRuntimeConfig` | 设备调用方 | 轮询、命令与事务超时、恢复策略、角度映射、L30 周期上报策略 |
| `HandDeviceConfig` | `HandDevice` | 已解析的型号、侧别、接口、地址和全部运行策略 |

通过 discovery 创建时，`DiscoveryCandidate` 写入型号、侧别、接口、设备地址和
HostID；`HandRuntimeConfig` 不包含这些字段，因此不能覆盖已经发现的物理端点。
手动创建 `HandDevice` 时，调用方仍可直接完整填写 `HandDeviceConfig`。

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

`create_hand_device()` 只创建关闭状态对象，不创建 Transport，也不产生总线 I/O。
`open()` 会重新读取身份并建立安全基线。候选中的 capabilities 只用于展示发现结果，
不会替代打开阶段的身份和能力验证。

## Partial candidate

默认的 `make_config()` 和 `create_hand_device()` 只接受 Verified candidate。
只有所有问题码都为 `missing_side` 的 Partial candidate 可以通过显式 side 补全：

```cpp
const auto &candidate = report.require_one(true);
auto hand = candidate.create_hand_device(runtime, HandSide::kLeft);
hand->open();
```

side 应来自部署配置或用户选择。规则如下：

- 只能传 `HandSide::kLeft` 或 `HandSide::kRight`；
- 已发现的 side 或经典 CAN 地址与请求 side 冲突时，在创建阶段拒绝；
- discovery 时没有 side 证据、但 `open()` 时设备报告了冲突 side 时，抛出
  `IdentityMismatchError`；
- 设备协议始终不报告 side 时，显式 side 作为调用方确认值，其他可用物理身份
  （product code、serial number、device UID）仍然必须匹配；
- 型号未知、节点/端点缺失、地址冲突或任何非 `missing_side` 问题继续抛出
  `IncompleteIdentityError`。

## 身份生命周期

发现到的身份在 `HandDevice` 对象内持久保存：

1. 首次 `open()` 校验型号、侧别及可用的 product code、serial number、device UID；
2. 手动创建的设备在首次成功打开后固定相同字段；
3. `close()` 只清理当前连接代缓存，不清理对象级身份约束；
4. 重新打开、主动 `get_device_info()` 和 `recover()` 使用同一约束；
5. 身份不一致时不会进入 Ready，控制命令也不会被接纳。

## 主要接口

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

无参数重载继续使用默认运行策略，已有调用方不需要修改。

## 可选依赖、驱动和权限

### Linux SocketCAN

- 需要内核 SocketCAN 驱动以及已创建、处于 UP 状态的 CAN 网络接口；常见部署还会
  使用 `iproute2` 在 SDK 外配置接口。
- 应用进程必须有权限打开对应 CAN raw socket。创建接口、设置 bitrate 或将接口置为
  UP 通常需要管理员能力，但 discovery 本身不会执行这些系统配置命令。
- Classic CAN 与 CAN FD 的 bitrate、FD 开关等均应在调用 SDK 前完成配置。

### MiniCANFD

- Linux 默认运行时库名为 `libcanbus.so`，Windows 为 `HCanbus.dll`；也可以使用
  `DiscoveryConfig::minicanfd.library_path` 或 `LINKERBOT_CANFD_LIB` 指定路径。
- Linux 应显式配置设备/通道布局；Windows 可按扫描到的设备使用默认通道 0。错误的
  布局会报告 `channel_layout_required` 或枚举失败。
- 动态库缺失只影响 MiniCANFD source，不影响核心 SDK 和其他 backend。

### PCAN 与 SLCAN

默认 PCAN Source 运行时加载 PCAN-Basic，使用 `ATTACHED_CHANNELS_COUNT` / `ATTACHED_CHANNELS`
枚举真实连接通道，保留原生 handle、硬件信息、FD 能力和占用状态。没有固定通道盲扫。
`config.pcan.library_path`（UTF-8 filesystem 路径）优先于 `LINKERBOT_PCAN_LIB`。
缺库、缺符号、能力未知、通道占用均保留诊断；Classic-only / 未知 FD 能力不会初始化 FD。

```cpp
DiscoveryConfig config;
config.backends = {"pcan"};
config.pcan.channels = {"PCAN_USBBUS2"}; // 使用枚举实际返回的通道
config.pcan.probe_protocols = {TransportKind::kCanFd};
config.pcan.nominal_bitrate = 1'000'000;
config.pcan.data_bitrate = 5'000'000;
config.pcan.fd_timing = linkerbot::can::PcanFdTiming{};
auto report = discover_hands(config);
```

默认 FD 时序仅为可精确产生 1/5 Mbit/s 的工程配置，仍须按硬件核实。
通道描述和 connection key 保存配置标识；factory 捕获枚举使用的实际模块、原生通道、
协议、速率和时序。Source 销毁或调用方配置变化不会令 candidate 退回 SocketCAN。
`create_hand_device()` 保持关闭、零 I/O；`open()` 重新核验身份。

Source 及手动 factory 的构造不加载驱动；`discover_hands()` 才枚举与探测。
单个 backend 失败保留其他 backend 的结果，并共享绝对截止时间。枚举/初始化返回后
会再次检查预算，超时后不启动身份探测。厂商同步枚举、初始化、关闭 API 没有取消参数，
SDK 无法强行中断卡住的驱动；总耗时可能超过 `total_timeout`，真实驱动上界待 Windows 验收。

PCAN/SLCAN 原有枚举器和 Transport factory 注入接口继续可用。
默认 SLCAN 仅查询 `config.slcan.channels` 指定的串口元数据再独占探测；空允许列表
返回 `allowlist_required`，公共 `config.channels` 只是结果过滤，不能代替串口访问许可。
默认 Lawicel Classic 配置只接受 `probe_protocols={TransportKind::kCan}`，不支持 FD/BRS。
使用 CANable FD 固件时，显式设置 `config.slcan.protocol=SLCANProtocol::kCanableFd`，
可选择 Classic 或 CAN FD 探测，并设置实际 nominal/data bitrate；FD 扩展需配套 comm
提供 `LINKERBOT_COMM_SLCAN_FD=1`。协议与配置示例见 [适配器接入](windows-adapters.md)。
旧版 comm 缺少 SLCAN 后端时返回 `backend_unavailable`。

```cpp
DiscoveryConfig config;
config.backends = {"slcan"};
config.slcan.channels = {"COM10"}; // 仅加入已确认的 Lawicel Classic 适配器
config.slcan.serial_baud = 115200;
config.slcan.nominal_bitrate = 1'000'000;
config.slcan.command_timeout = std::chrono::milliseconds{500};
auto report = discover_hands(config);
```

不扫描其他 COM 口；候选 factory 捕获上述配置，不受随后修改 config 影响。
PCAN、SLCAN 与 MiniCANFD 的 PowerShell 只读步骤、
完整 FD 时序和六型号注入方式见 [Windows 适配器接入](windows-adapters.md)。

## 失败诊断

发现报告可以同时包含成功候选与失败。调用方应记录 failure，而不是仅检查候选数量：

```cpp
const auto report = discover_hands(config);
for (const auto &failure : report.failures()) {
    std::cerr << "backend=" << failure.backend().value_or("unknown") << " stage=" << failure.stage().value_or("unknown")
              << " channel=" << failure.native_channel().value_or("-") << " error=" << failure.message() << '\n';
}
```

常见错误码：

| 错误码 | 含义/处理建议 |
| --- | --- |
| `backend_not_found` | 请求了未注册 backend；检查 `backends` 拼写和平台组合 |
| `backend_unavailable` | 可选适配层未注入或驱动不可用 |
| `platform_backend_unavailable` | 当前平台没有默认 source；显式提供自定义 source |
| `channel_not_found` | 显式请求的通道未被枚举到 |
| `unsupported_protocol` | 通道不支持请求的 Classic CAN/CAN FD 协议 |
| `timeout` | 全局发现预算耗尽；缩小范围或调整 `total_timeout` |
| `backend_busy` | 通道被占用；关闭其他客户端后重试 |
| `transport_io` | Transport 打开或收发失败；结合 native status 和驱动日志排查 |
| `channel_layout_required` | MiniCANFD 等 backend 缺少明确设备/通道布局 |
| `backend_failed` | 驱动加载、枚举或 Transport 初始化失败；结合底层状态排查 |
| `channel_descriptor_conflict` | 多 source 对同一物理通道给出冲突描述 |
| `no_response` | 通道可打开，但探测地址没有身份响应 |

## 安全边界

- discovery 只发送身份读取请求，不调用 `enable()`，不发送角度、力矩、电流、参数
  写入或其他运动控制命令。
- discovery 不执行 `ip link` 等系统网络配置命令，也不创建、启停或修改 SocketCAN
  接口；SocketCAN bitrate 必须由部署方预先配置。
- MiniCANFD 临时 Transport 初始化会把配置中明确给出的 nominal/data bitrate 传给
  厂商驱动，这是打开该适配器所需的初始化步骤；扫描完成后会关闭临时 Session 与
  Transport。不要把未知总线接入不匹配的 bitrate 配置。
- 创建 source、配置或 `DiscoveryCandidate` 不产生 I/O。调用 `discover_hands()` 才会
  枚举并发送身份查询；`create_hand_device()` 仍为零 I/O。
- `HandDevice::open()` 会再次读取物理身份、校验 discovery 固定的身份并建立安全
  基线。身份不一致时不会进入 Ready，也不会接纳控制命令。

真实适配器验收步骤和证据模板见
[实机验收中的设备发现检查](hardware-acceptance/README.md#设备发现检查)；
发行包使用者可在源码仓库的 `docs/hardware-acceptance/README.md` 查看该清单。
