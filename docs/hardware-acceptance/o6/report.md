# O6 实机验收报告

结论：**O6 已完成 SDK 常用接口实机验证，33 个常用用例最终通过**（分组执行，包含 raw 角度复测）。高级调参、恢复出厂和 EEPROM 保存不纳入本次验收。

2026-10-19 已追加 Windows / SLCAN Classic / Debug 实测：枚举、发现、发现连接、显式连接、六关节读取、关闭重连及单关节运动恢复通过，**6 个用例、416 条断言通过，无失败或跳过**，详见文末。

同日 Windows / PCAN Classic / Debug 也已完成上述接入与单关节运动恢复检查，另有 **6 个用例、416 条断言通过，无失败或跳过**。两种适配器路径分别保留实测记录。

## 环境

- 日期：2026-10-14，UTC+08:00。
- O6 左手，CAN 地址 `0x28`；产品码 `LHO6-04-021-L-Z-1-E`，序列号 `021`。
- 固件 `V1.3.2`，硬件 `V0.0.3`，机械版本 `V0.0.1`。
- Ubuntu 24.04.3 x86_64，Linux `7.0.0-38-generic`，GCC 13.3；SocketCAN `can0`，经典 CAN，1 Mbit/s。
- 适配器 USB 产品名 `XCAN-USB`，VID:PID `0c72:000c`，Linux 驱动 `pcan_usb`；不据 USB 标识推断其实际生产厂商。
- hand：`3be362e82b8d088bc64827d51c2ffa4ee06564a4` 加本轮修复；comm：`18f7e0e87f4710fc68af7b66d6cb5cdf97018b96`。本轮修复由工单 #200 跟踪，冻结最终发布源码后需复验。

## 结果

| 项目 | 结果 |
| --- | --- |
| 身份、生命周期、读取、输入校验、触觉、轮询、快照、事件流与不支持操作的拒绝 | 20 个用例、2700 条断言通过。 |
| raw / percent 逐关节运动与综合角度验收 | 6 个角度用例最终均通过，其中 3 个读取/校验用例已计入上一行；两种命令均覆盖全部 6 个关节。 |
| 回执、缺 ACK 超时、参数复制、连续目标替换、发送失败、关闭/恢复取消和旧目标不重放 | 7 个用例、380 条断言通过。缺 ACK 测试使用已知限流基准，未借此写入高级参数。 |
| 速度、限流逐通道变更与恢复；恢复连接后旧配置回复失效 | 2 个用例、982 条断言通过，各覆盖 6 通道。验证依据为设备写回复，不能记为独立配置查询。 |
| 真实断电与重连 | 1 个用例、34 条断言通过；保持 USB 连接，仅切换手部供电。读取失败后，同一对象恢复身份、连接代次、轮询和新反馈，无需重置系统 CAN 接口。 |
| 离线回归 | CTest 4/4 通过，包含本轮序列号和速度 getter 回归。 |

角度采用默认步幅 5%、容差 2%、等待 5 秒，要求连续两次反馈达标；步幅大于两倍容差，静止不能通过。raw 首轮侧摆恢复目标 181、反馈 175，差 6 raw，超过约 5.1 raw 容差；保持原容差复测完整六关节，274 条断言全部通过。首次失败记录保留，不将其改为通过。

## 本轮修复与结束状态

- 序列号最后一帧实测 DLC=3，修复此前强制 DLC=8 导致的身份读取超时；仅允许最后一段省略填充，仍拒绝截短的中间段，并按实际 DLC 读取。
- 固件速度写入 20 后，单字节 `0x05` 查询返回 0；查询后不能运动，写入 20 后不查询则能正常运动。统一 `get_configured_speeds()` 改为只读取设备写回复，避免查询改变速度；首次写入前缓存不可用，恢复连接后失效。型号专用旧 `O6::speed.get_blocking()` 仍发送该查询，V1.3.2 应使用统一接口。
- 用户确认原速度、限流未知：结束保留六关节速度 raw=20、限流 raw=60；断电测试后已重新建立并核对写回复，未保存 EEPROM，不声称恢复未知原值。
- 最后下发开始姿态 `[254,182,254,254,254,254]`，反馈 `[254,181,253,253,253,253]`，最大偏差 1 raw；故障全 0，温度最高 41°C。

## 复现命令

在 SDK 根目录构建，并按 [硬件测试说明](../../../tests/hand/device/hardware/README.md) 建立 CAN 连接：

```bash
cmake --build build --target tests --parallel 4
export HAND_DEVICE_MODEL=o6 HAND_DEVICE_SIDE=left HAND_DEVICE_ADDRESS=0x28 CAN_INTERFACE=can0
export HAND_DEVICE_ENABLE_MOTION_TESTS=1 HAND_DEVICE_ENABLE_FAULT_TESTS=1
export HAND_DEVICE_ENABLE_PARAMETER_TESTS=1
export HAND_DEVICE_BASELINE_SPEEDS=20,20,20,20,20,20
export HAND_DEVICE_BASELINE_CURRENT_LIMITS=60,60,60,60,60,60
# 先建立已确认的速度/限流基准；仅执行这两个参数 section。
./build/tests/tests '[hardware][device][parameters][write]' --section set_speeds --section set_current_limits --order decl
./build/tests/tests '[hardware][device][read-only],[hardware][device][angles][validation],[hardware][device][control]~[persistent]' --order decl
./build/tests/tests '[hardware][device][angles]' --order decl
./build/tests/tests '[hardware][device][commands]' --order decl
```

真实断连按硬件测试说明执行，本轮每步等待 15 分钟。原始日志和临时准备程序保存在仓库外；本目录仅维护这一份报告。

## Windows SLCAN Classic 实测（2026-10-19，UTC+08:00）

本场验证 O6 的 Windows 串口接入及一次单关节运动恢复，不重复 Linux 的全关节、参数、
故障注入及长期负载验收。CLI 枚举、发现、发现连接、显式连接及运动后再次显式连接均退出码 0；
**6/6 个实机用例、416 条断言通过，0 失败、0 跳过**。

### 环境与设备

- Windows 11 家庭版中文版 x64，`10.0.26200`；MSVC v143 14.44、Windows SDK `10.0.26100.0`、CMake `3.31.8`、Catch2 `3.8.1`，Debug `/MDd`。
- hand：`a6d5a73c4faf844d7ff2a7aa4d996c28dcc6d1eb`，含未提交的文档及 Linux L6 示例整理；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`，工作区干净。本场未修改 SDK 或硬件测试源码，复用本日 L20 已验证构建；测试程序、只读入口及两个 SDK 静态库的 SHA256 与该场次一致。
- 适配器：COM11，USB VID:PID=`16d0:117e`，USB 序列字符串 `3472396E3335`；Microsoft `usbser.inf`，驱动 `10.0.26100.9278`。具体商业型号与固件版本未报告，与本日 L6/L20 Classic 场次为同一根适配器。
- 配置：`slcan / canable-fd`，**Classic CAN** 1 Mbit/s，UART 115200，串口超时 500 ms，启动等待 100 ms；未设置 FD 数据速率，不使用厂商 CAN DLL，未刷写适配器固件。
- 本场身份实读：O6 左手，地址 `0x28`，序列号 `021`，产品码 `LHO6-04-021-L-Z-1-E`，固件 `V1.3.2`、硬件 `V0.0.3`、机械 `V0.0.1`，6 关节。型号及侧别先通过发现确认，再进行显式连接。
- 供电、接线与终端电阻规格未独立测量；CLI 使用场次隔离配置，Catch2 入口使用构建目录 `tests/config`，未加载用户映射。运行前后映射配置均有副本。

### 运行与结果

| 项目 | 结果 | 原始证据 |
| --- | --- | --- |
| COM 枚举 / 发现 | COM11 可枚举，发现一个 O6 左手 Verified 候选，`model=2 side=1 status=0` | `list.log`、`discover.log` |
| 发现连接 / 显式连接 | 身份一致，均读取 6 个角度并正常关闭 | `read.log`、`manual.log` |
| `HandDevice hardware identity refresh and cache` | 1 用例、69 断言通过；身份刷新、缓存及重开一致 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、67 断言通过；六关节反馈有效、非 stale，连续采样递增 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；关闭、重开及后续反馈正常 | `lifecycle.log/.xml` |
| `HandDevice hardware faults and snapshot projection` | 1 用例、71 断言通过；故障读取、解析及快照投影一致 | `faults.log/.xml` |
| `HandDevice hardware raw angle commands` | 1 用例、102 断言通过；关节 0 `thumb_flex`，基准 raw=254、目标 raw=241；目标反馈及恢复通过，`joints=1/6` | `angle-smoke.log/.xml` |
| 运动后显式连接 / 再次角度读取 | CLI 退出码 0；角度用例 1 个、67 断言通过 | `manual-after.log`、`angles-after.log/.xml` |

运动仅选关节 0，步幅 5%、容差 2%、5000 ms 到位上限，未启用初始姿态修正。
命令携带六关节向量，其他关节保持采样基准；实际反馈到位及恢复均由查询验证，
不把串口写入或发送终态当作机械到位。运动后 CLI 实读角度为 `[254, 254, 253, 253, 253, 253]`。
未发送整手 50%/100% 目标，未写速度、限流、清故障或持久化参数，也未调用型号专用的旧速度查询接口。
没有新增人工运动观察记录，不据此声明全关节或长期性能已重新验收。

### 复现与证据

只读入口（工作区根目录，按实际构建路径及端口调整）：

```powershell
$Read='.\build-windows-l6-clean\examples\Debug\windows_readonly.exe'
$Cli=@('--backend','slcan','--channel','COM11','--mode','classic','--slcan-protocol','canable-fd',
  '--bitrate','1000000','--serial-baud','115200','--serial-timeout-ms','500','--serial-startup-ms','100',
  '--model','o6','--side','left','--address','0x28')
& $Read --list @Cli
& $Read --discover @Cli
& $Read --read @Cli
& $Read --manual @Cli
```

硬件用例使用 `HAND_DEVICE_MODEL=o6`、`HAND_DEVICE_SIDE=left`、`HAND_DEVICE_ADDRESS=0x28`、
`HAND_DEVICE_BACKEND=slcan`、`HAND_DEVICE_CHANNEL=COM11`、`HAND_DEVICE_SLCAN_PROTOCOL=canable-fd`，
`HAND_DEVICE_BITRATE=1000000`、`HAND_DEVICE_SERIAL_BAUD=115200`、
`HAND_DEVICE_SERIAL_TIMEOUT_MS=500`、`HAND_DEVICE_SERIAL_STARTUP_MS=100`。
先完成上述只读用例，再设置 `HAND_DEVICE_ENABLE_MOTION_TESTS=1`，
以关节 0、5% 步幅、2% 容差、5000 ms 上限运行 `HandDevice hardware raw angle commands`，结束后关闭运动开关。

原始证据及 `run-o6-slcan-classic.ps1`（仓库外）：
`acceptance/o6-slcan-classic-20261019-114707`，含双仓 SHA/状态/差异、
二进制哈希、串口/驱动/系统、CLI 与用例日志、JUnit、环境变量/退出码、映射配置和 `results.json`。
USB 拔插、PCAN、Release、其他手型/固件及长期负载未在本场测试。

## Windows PCAN Classic 实测（2026-10-19，UTC+08:00）

本场更换为 PCAN-USB，验证 O6 的 Windows 原生驱动接入及一次单关节运动恢复。
14:37:25–14:38:16，CLI 枚举、发现、发现连接、显式连接及运动后再次显式连接均退出码 0；
**6/6 个实机用例、416 条断言通过，0 失败、0 跳过**。不重复 Linux 通用功能或全关节验收。

### 环境与设备

- Windows 11 家庭版中文版 x64，`10.0.26200`；MSVC v143 14.44、Windows SDK `10.0.26100.0`、CMake `3.31.8`、Catch2 `3.8.1`，Debug `/MDd`。
- hand：`a6d5a73c4faf844d7ff2a7aa4d996c28dcc6d1eb`，含未提交的文档及 Linux L6 示例整理；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`，工作区干净。本场未修改 SDK 或硬件测试源码，复用本日已验证构建；测试程序、只读入口及两个 SDK 静态库的 SHA256 与 L30 场次一致。
- PCAN-USB，USB VID:PID=`0c72:000c`，设备实例 `USB\VID_0C72&PID_000C\5&7D1F3A1&0&6`；PEAK-System 驱动 `5.0.0.20000`，`oem302.inf`。适配器序列号及固件未报告。
- 配置：`pcan / PCAN_USBBUS1`，**Classic CAN** 1 Mbit/s；枚举显示 `FD=no condition=1 owned=0`，不设置 FD 时序或数据速率，不使用 COM 串口。
- x64 DLL：`C:\Windows\System32\PCANBasic.dll`，版本 `5.0.0.1115`，SHA256 `6FCEB8F08837F483925C10F7BCBFA8D6BBF8DF51E10CBC6F161C9DD45D0F8871`。
- 本场身份实读：O6 左手，地址 `0x28`，序列号 `021`，产品码 `LHO6-04-021-L-Z-1-E`，固件 `V1.3.2`、硬件 `V0.0.3`、机械 `V0.0.1`，6 关节。发现与显式连接身份一致。
- 供电、接线及终端电阻规格未独立测量；CLI 使用场次隔离配置，Catch2 入口使用构建目录 `tests/config`，未加载用户映射。运行前后映射配置均有副本。

### 运行与结果

| 项目 | 结果 | 原始证据 |
| --- | --- | --- |
| 适配器枚举 / 发现 | PCAN_USBBUS1 可用，发现一个 O6 左手 Verified 候选，`model=2 side=1 status=0` | `list.log`、`discover.log` |
| 发现连接 / 显式连接 | 身份一致，均读取 6 个角度并正常关闭 | `read.log`、`manual.log` |
| `HandDevice hardware identity refresh and cache` | 1 用例、69 断言通过；身份刷新、缓存及重开一致 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、67 断言通过；六关节反馈有效、非 stale，连续采样递增 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；关闭、重开及后续反馈正常 | `lifecycle.log/.xml` |
| `HandDevice hardware faults and snapshot projection` | 1 用例、71 断言通过；故障读取、解析及快照投影一致 | `faults.log/.xml` |
| `HandDevice hardware raw angle commands` | 1 用例、102 断言通过；关节 0 `thumb_flex`，基准 raw=254、目标 raw=241；目标反馈及恢复通过，`joints=1/6` | `angle-smoke.log/.xml` |
| 运动后显式连接 / 再次角度读取 | CLI 退出码 0；角度用例 1 个、67 断言通过 | `manual-after.log`、`angles-after.log/.xml` |

运动仅选关节 0，步幅 5%、容差 2%、5000 ms 到位上限，未启用初始姿态修正。
命令携带六关节向量，其他关节保持采样基准；角度命令检查发送完成终态，实际到位及恢复由查询验证。
结束 CLI 实读角度为 `[254, 254, 253, 253, 253, 253]`，恢复检查通过，连接正常关闭。
未发送整手 50%/100% 目标，未写速度、限流、清故障或持久化参数，也未调用型号专用的旧速度查询接口。
没有新增人工运动观察记录，不据此声明全关节或长期性能已重新验收。

### 复现与证据

只读入口（工作区根目录，按实际构建路径、通道及 DLL 调整）：

```powershell
$Read='.\build-windows-l6-clean\examples\Debug\windows_readonly.exe'
$Cli=@('--backend','pcan','--channel','PCAN_USBBUS1','--mode','classic',
  '--library','C:\Windows\System32\PCANBasic.dll','--bitrate','1000000',
  '--model','o6','--side','left','--address','0x28')
& $Read --list @Cli
& $Read --discover @Cli
& $Read --read @Cli
& $Read --manual @Cli
```

硬件用例设置 `HAND_DEVICE_MODEL=o6`、`HAND_DEVICE_SIDE=left`、`HAND_DEVICE_ADDRESS=0x28`、
`HAND_DEVICE_BACKEND=pcan`、`HAND_DEVICE_CHANNEL=PCAN_USBBUS1`、
`HAND_DEVICE_LIBRARY=C:\Windows\System32\PCANBasic.dll`、`HAND_DEVICE_BITRATE=1000000`。
先完成上述只读用例，再开启 `HAND_DEVICE_ENABLE_MOTION_TESTS=1`，
以关节 0、5% 步幅、2% 容差、5000 ms 上限运行 `HandDevice hardware raw angle commands`，结束后关闭运动开关。

原始证据及 `run-o6-pcan.ps1`（仓库外）：
`acceptance/o6-pcan-20261019-143704`，含双仓 SHA/状态/差异、
二进制及 DLL 哈希、适配器/驱动/系统、CLI 与用例日志、JUnit、环境变量/退出码、映射配置和 `results.json`。
USB 拔插、PCAN FD、Release、其他手型/固件及长期负载未在本场测试。
