# Windows 适配器接入

本文说明如何通过 PCAN、MiniCANFD 或 SLCAN 读取设备，再将连接方式用于自己的 C++ 程序。
各型号的实测范围见 [平台与硬件验证范围](COMPATIBILITY.md#平台与硬件验证范围)。
L6 默认使用 Classic CAN，下文的 L6 示例按此配置；支持 CAN FD 的设备可按
[可选协议说明](usage.md#l6-可选-can-fd-协议)显式选择 HOP，并使用匹配的 FD 适配器。

## 1. 准备环境

先按 [Windows 构建步骤](windows.md#构建-sdk)生成 `windows_readonly.exe`。
厂商驱动和 DLL 需要单独安装，DLL、程序均使用 x64。

| 适配器 | 需要准备 | 通道示例 |
| --- | --- | --- |
| PCAN | PEAK 驱动、`PCANBasic.dll`；连接 FD 设备时需要支持 FD 的型号 | `PCAN_USBBUS1` |
| MiniCANFD | 厂商驱动、`HCanbus.dll` 及其配套依赖 | `0:0`，即设备索引:通道索引 |
| SLCAN | 串口驱动，确认固件使用 Lawicel Classic 还是 CANable FD 协议 | `COM10` |

在源码根目录的 PowerShell 中设置程序路径：

```powershell
$Read = '.\build-windows\examples\Release\windows_readonly.exe'
& $Read --help
```

使用 Debug 构建时，将路径中的 `Release` 改为 `Debug`。下面的 DLL 路径、通道、型号、
左右手、地址和速率均按实际设备修改；命令退出码 `$LASTEXITCODE` 应为 `0`。

## 2. 选择适配器并读取

每种适配器都先枚举，再读取。`--read` 会发现设备、读取身份和角度，然后关闭连接，
不发送运动目标。需要单独发现时将它换成 `--discover`；已知型号、侧别和地址时可换成
`--manual`，跳过发现直接连接。每次只能选择一种动作。

### PCAN Classic

适用于 Classic 固件的 L6、O6、L20 Lite、L20。先枚举，再将通道替换为实际结果。

```powershell
$Pcan = 'C:\vendor\PCANBasic.dll'
& $Read --list --backend pcan --library $Pcan

$Can = @('--backend','pcan','--channel','PCAN_USBBUS1','--mode','classic',
  '--library',$Pcan,'--bitrate','1000000')
& $Read --read @Can --model l6 --side left --address 0x28
```

### MiniCANFD

适用于 L30、O30。使用枚举输出的设备与通道索引；Windows 下通道固定为 `0`。
DLL 能枚举设备并不保证能收发，出现发送失败时检查 DLL 是否与设备配套。

```powershell
$Mini = 'C:\vendor\HCanbus.dll'
& $Read --list --backend minicanfd --library $Mini

$Can = @('--backend','minicanfd','--channel','0:0','--mode','fd',
  '--library',$Mini,'--bitrate','1000000','--data-bitrate','5000000')
& $Read --read @Can --model l30 --side right --address 1
```

### SLCAN / CANable FD

下面以 CANable FD 固件连接 L30 为例。COM 口取自设备管理器，仅访问指定端口，无需厂商 DLL。

```powershell
$Can = @('--backend','slcan','--channel','COM10','--mode','fd',
  '--slcan-protocol','canable-fd','--serial-baud','115200',
  '--bitrate','1000000','--data-bitrate','5000000','--serial-timeout-ms','500')
& $Read --list @Can
& $Read --read @Can --model l30 --side right --address 1
```

| 设备与固件 | 参数调整 |
| --- | --- |
| O30 + CANable FD | 改为 `--model o30`，填写实际侧别和 HOP 请求 ID |
| L6／O6／L20 Lite／L20 + CANable FD | 保留 `--slcan-protocol canable-fd`，改为 `--mode classic`，去掉 `--data-bitrate`，修改型号、侧别和地址 |
| Classic 手型 + Lawicel 固件 | 使用 `--slcan-protocol lawicel-classic --mode classic`，不传 `--data-bitrate` |

`--serial-baud` 是串口速率，`--bitrate` 是 CAN 速率，分别与固件和设备配置匹配。
Lawicel Classic 不支持 FD；CANable FD 的数据速率支持 2M 或 5M。
若串口打开后需要稳定时间，可添加 `--serial-startup-ms 100`。

### PCAN FD

适用于支持 FD 的 PCAN 适配器连接 L30、O30，目前尚未完成实机验证。
需确认枚举结果为 `FD=yes`，并配置与设备匹配的位时序。下例为 80 MHz 时钟、1M/5M 速率：

```powershell
$Pcan = 'C:\vendor\PCANBasic.dll'
& $Read --list --backend pcan --library $Pcan

$Can = @('--backend','pcan','--channel','PCAN_USBBUS2','--mode','fd',
  '--library',$Pcan,'--bitrate','1000000','--data-bitrate','5000000',
  '--fd-clock','80000000','--nom-brp','1','--nom-tseg1','63','--nom-tseg2','16','--nom-sjw','16',
  '--data-brp','1','--data-tseg1','11','--data-tseg2','4','--data-sjw','4')
& $Read --read @Can --model l30 --side right --address 1
```

L30 使用扩展 FD，`--address` 为 NodeID；O30 使用标准 FD、无 BRS，`--address` 为 HOP 请求 ID。
帧格式由 SDK 按型号选择，通道名称与设备地址分别配置。

## 3. 在 C++ 程序中连接

Windows 需要向 `HandDevice` 传入适配器 factory（连接工厂）或共享 Session。
下面是 PCAN Classic 连接 L6 左手的完整示例：

```cpp
#include <linkerbot/hand/adapter_transport.hpp>
using namespace linkerbot;

int main() {
    can::PcanConfig pcan;
    pcan.channel = can::pcan_channel_handle("PCAN_USBBUS1");
    pcan.library_path = "C:/vendor/PCANBasic.dll";
    pcan.nominal_bitrate = 1'000'000;

    hand::HandDeviceConfig config;
    config.model = hand::Model::kL6;
    config.side = hand::HandSide::kLeft;
    config.interface_name = "pcan:PCAN_USBBUS1";
    config.address = 0x28;

    hand::HandDevice device(config, hand::make_pcan_transport_factory(pcan));
    device.open();
    const auto info = device.get_device_info();
    const auto angles = device.get_angles();
    device.close();
}
```

更换适配器时，替换配置和 factory，并同步调整型号、侧别、地址及 `interface_name`：

| 适配器 | 配置类型 | 创建 factory |
| --- | --- | --- |
| PCAN | `can::PcanConfig` | `hand::make_pcan_transport_factory(pcan)` |
| MiniCANFD | `can::VendorCanFdTransport::Config` | `hand::make_minicanfd_transport_factory(mini)` |
| SLCAN | `can::SlcanConfig` | `hand::make_slcan_transport_factory(serial)` |

MiniCANFD 设置 `device_index`、`channel_index`、`library_path`、`nom_baud` 和 `dat_baud`。
SLCAN 设置 `port`、`serial_baud`、`nominal_bitrate`；CANable FD 固件还需设置
`protocol = can::SlcanProtocol::kCanableFd`，按设备选择 `fd` 和 `data_bitrate`。
PCAN FD 需设置 `mode = can::PcanMode::kFd`、双速率及 `fd_timing`。

`open()` 建立连接并核验身份。读取成功后，控制与轮询使用同一套
[HandDevice API](usage.md#读取与控制)。
也可以通过[设备发现接口](hand-device-discovery.md)创建已绑定适配器配置的设备。

## 角度映射配置目录

可通过 `config.angle_mapping_path` 指定文件路径。未指定时，依次查找
`XDG_CONFIG_HOME`、`LOCALAPPDATA`、`APPDATA`，最后使用 `HOME` 或 `USERPROFILE` 下的 `.config`。
默认文件为所选目录下的 `linkerbot/hand_angle_mappings.toml`，支持中文路径。

## 常见问题

| 现象 | 优先检查 |
| --- | --- |
| DLL 加载失败 | DLL 是否为 x64、路径是否正确、配套依赖是否齐全 |
| 枚举成功但无法读取 | 设备供电、接线、终端电阻、CAN 速率、设备地址及驱动／DLL 版本 |
| 通道或串口被占用 | 关闭占用同一通道的其他控制程序 |
| SLCAN 提示 `allowlist_required` | 显式传入实际 COM 口；使用发现 API 时设置 `discovery.slcan.channels` |
| SLCAN FD 配置被拒绝 | 固件需兼容 CANable FD，配套 transport 库需支持 FD 扩展 |
| Windows 使用 `can0` 无法连接 | 使用 Windows 适配器 factory 或发现接口创建设备 |
