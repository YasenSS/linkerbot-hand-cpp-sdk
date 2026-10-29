# L20 Lite 实机验收报告

结论：**已完成 SDK 常用接口实机验证**。按开源 TODO 已确认的范围验证命令、回执、反馈、配置缓存和恢复；30 个常用用例通过，3 个角度运动用例保留到位/联动失败，未改记为通过。设备运动精度、高级调参、恢复出厂和 EEPROM 保存不纳入本项。

2026-10-19 已追加 Windows / SLCAN Classic / Debug 实测：枚举、发现、发现连接、显式连接、10 关节读取、关闭重连及关节 0 小幅运动恢复通过，**6 个用例、552 条断言通过，无失败或跳过**。本场不复测关节 7/8，历史到位与联动失败记录保留，详见文末。

同日 Windows / PCAN Classic / Debug 也已完成上述接入与关节 0 运动恢复检查，另有 **6 个用例、552 条断言通过，无失败或跳过**。两条路径分别记录，不据此将历史关节 7/8 问题改记为通过。

## 环境

- 日期：2026-10-14，UTC+08:00；L20 Lite 左手，10 关节，CAN 地址 `0x28`。
- 产品码 `LHT10-07-0-L-B-1-A`，序列号设备上报为 `0`；固件 `V4.6.10`，硬件 `V1.1.1`，机械版本 `V0.0.7`。
- Ubuntu 24.04.3 x86_64，Linux `7.0.0-38-generic`，GCC 13.3；SocketCAN `can0`，经典 CAN，1 Mbit/s。
- USB 产品名 `XCAN-USB`，VID:PID `0c72:000c`，驱动 `pcan_usb`；不据 USB 标识推断实际生产厂商。
- hand 基线 `91beb24cb87b1144e9cb6ecb099cd4ccb30d3019` 加本轮修复；comm `18f7e0e87f4710fc68af7b66d6cb5cdf97018b96`。本轮修复由工单 #202 跟踪，冻结最终发布源码后需复验。

## 结果

| 项目 | 结果 |
| --- | --- |
| 身份、生命周期、读取、输入校验、触觉、轮询、快照、事件流与不支持操作的拒绝 | 修复后 20 个用例、3903 条断言通过。 |
| 回执、缺 ACK 超时、参数复制、连续目标替换、发送失败、取消与旧目标不重放 | 7 个用例、442 条断言通过。 |
| 速度、限流逐通道变更/恢复与配置缓存恢复失效 | 2 个用例、1696 条断言通过，各覆盖全部 10 通道；验证依据是设备写回复，非独立配置查询。 |
| raw / percent 角度命令 | 全部 10 关节的两种方式均执行。关节 0–6、9 到位并恢复；关节 8 加大步幅后自身到位，但整手保持检查失败；关节 7 未到位。 |
| 角度失败记录 | 默认步幅 5%、容差 2%，综合用例在关节 7 失败，后续关节由独立用例补齐。关节 7 的 raw 目标 157、实际 168；反向目标 194、实际 172，也未到位。关节 8 复测 raw 目标 222、实际 221，percent 对应目标 223、实际 223，但关节 7 分别从 171/168 偏移到 130/132，整手断言仍失败。原因未确定，不凭这些结果认定 SDK 编码错误或硬件故障。 |
| 真实断电恢复 | 1 个用例、34 条断言通过；仅切换手部供电，USB 保持连接，无需重置系统 CAN 接口。同一对象恢复身份、连接代次、轮询与新反馈。 |
| 离线回归 | CTest 4/4 通过，含帧间隔、缓存分离、固件能力以及取消/批次回归。 |

角度默认等待 5 秒并要求连续两次反馈达标；关节 8 复测用 10% 步幅、4% 容差。两种配置均要求步幅大于两倍容差，静止不能通过。按用户既有要求，以反馈检查为主，未逐次要求现场观察。

## 修复与结束状态

- 双帧向量请求及写入统一为 1 ms 间隔，解决原 0/0.5 ms 间隔下的偶发缺帧；仍验证批次不交错，并允许停止轮询时取消尚未发送的后半帧。
- 实时速度与速度配置写回复分开保存；实时查询既不填充也不覆盖配置缓存，恢复后旧配置回复失效。
- `V4.6.10` 的 `0x20`–`0x23` 标量查询无回复，逐条以 200 ms 间隔复核仍无回复；已验证能力仅保留五指 `fn` 矩阵，显式拒绝不可用标量和法向合力。其他固件的声明能力保留。
- 速度实测 21/24 回复为 20/23，22/23/25 原样回复；仅对此固件的速度测试采用至少 3 raw 步幅、1 raw 容差，仍拒绝未改变的配置。
- 用户确认原速度/限流未知，结束保留全部 10 关节速度 raw=20、限流 raw=60；断电后重新建立并核对写回复，未保存 EEPROM。实时速度读为全 0 后，配置缓存仍为 20，随后小幅运动目标 241、反馈 241，确认查询后仍可控制。
- 最终开始姿态与反馈均为 `[254,127,254,254,254,254,0,170,249,112]`；故障全 0，温度最高 40°C。

## 复现命令

在 SDK 根目录构建，并按 [硬件测试说明](../../../tests/hand/device/hardware/README.md) 配置经典 CAN：

```bash
cmake --build build --target tests --parallel 4
export HAND_DEVICE_MODEL=l20lite HAND_DEVICE_SIDE=left HAND_DEVICE_ADDRESS=0x28 CAN_INTERFACE=can0
export HAND_DEVICE_ENABLE_PARAMETER_TESTS=1 HAND_DEVICE_ENABLE_MOTION_TESTS=1 HAND_DEVICE_ENABLE_FAULT_TESTS=1
export HAND_DEVICE_BASELINE_SPEEDS=20,20,20,20,20,20,20,20,20,20
export HAND_DEVICE_BASELINE_CURRENT_LIMITS=60,60,60,60,60,60,60,60,60,60
./build/tests/tests '[hardware][device][parameters][write]' --section set_speeds --section set_current_limits --order decl
./build/tests/tests '[hardware][device][read-only],[hardware][device][angles][validation],[hardware][device][control]~[persistent]' --order decl
./build/tests/tests '[hardware][device][commands]' --order decl
./build/tests/tests '[hardware][device][angles]' --order decl
# 综合用例失败后，将关节变量依次设为尚未覆盖的 8、9，补齐两种命令。
HAND_DEVICE_ANGLE_JOINT=8 ./build/tests/tests 'HandDevice hardware raw angle commands,HandDevice hardware percent angle commands' --order decl
```

真实断连按硬件测试说明执行，本轮每步等待 15 分钟。原始日志、被动 CAN 抓包和临时诊断程序均保存在仓库外；本目录仅维护这一份报告。

## Windows SLCAN Classic 实测（2026-10-19，UTC+08:00）

本场验证 L20 Lite 的 Windows 串口接入及一次关节 0 运动恢复，不重复 Linux 的全关节、参数、
故障注入及长期负载验收。CLI 枚举、发现、发现连接、显式连接及运动后再次显式连接均退出码 0；
**6/6 个实机用例、552 条断言通过，0 失败、0 跳过**。此结果不覆盖历史关节 7/8 的到位与联动问题。

### 环境与设备

- Windows 11 家庭版中文版 x64，`10.0.26200`；MSVC v143 14.44、Windows SDK `10.0.26100.0`、CMake `3.31.8`、Catch2 `3.8.1`，Debug `/MDd`。
- hand：`a6d5a73c4faf844d7ff2a7aa4d996c28dcc6d1eb`，含未提交的文档及 Linux L6 示例整理；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`，工作区干净。本场未修改 SDK 或硬件测试源码，复用本日 O6/L20 已验证构建；测试程序、只读入口及两个 SDK 静态库的 SHA256 与 O6 场次一致。
- 适配器：COM11，USB VID:PID=`16d0:117e`，USB 序列字符串 `3472396E3335`；Microsoft `usbser.inf`，驱动 `10.0.26100.9278`。具体商业型号及固件版本未报告，与本日 L6/O6/L20 Classic 场次为同一根适配器。
- 配置：`slcan / canable-fd`，**Classic CAN** 1 Mbit/s，UART 115200，串口超时 500 ms，启动等待 100 ms；未设置 FD 数据速率，不使用厂商 CAN DLL，未刷写适配器固件。
- 本场身份实读：L20 Lite 左手，地址 `0x28`，序列号设备上报为 `0`，产品码 `LHT10-07-0-L-B-1-A`，固件 `V4.6.10`、硬件 `V1.1.1`、机械 `V0.0.7`，10 关节。型号及侧别先通过发现确认，再进行显式连接。
- 供电、接线及终端电阻规格未独立测量；CLI 使用场次隔离配置，Catch2 入口使用构建目录 `tests/config`，未加载用户映射。运行前后映射配置均有副本。

### 运行与结果

| 项目 | 结果 | 原始证据 |
| --- | --- | --- |
| COM 枚举 / 发现 | COM11 可枚举，发现一个 L20 Lite 左手 Verified 候选，`model=3 side=1 status=0` | `list.log`、`discover.log` |
| 发现连接 / 显式连接 | 身份一致，均读取 10 个角度并正常关闭 | `read.log`、`manual.log` |
| `HandDevice hardware identity refresh and cache` | 1 用例、69 断言通过；身份刷新、缓存及重开一致 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、99 断言通过；10 关节反馈有效、非 stale，连续采样递增 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；关闭、重开及后续反馈正常 | `lifecycle.log/.xml` |
| `HandDevice hardware faults and snapshot projection` | 1 用例、95 断言通过；故障读取、解析及快照投影一致 | `faults.log/.xml` |
| `HandDevice hardware raw angle commands` | 1 用例、150 断言通过；关节 0 `thumb_flex`，基准 raw=254、目标 raw=241；目标反馈及恢复通过，`joints=1/10` | `angle-smoke.log/.xml` |
| 运动后显式连接 / 再次角度读取 | CLI 退出码 0；角度用例 1 个、99 断言通过 | `manual-after.log`、`angles-after.log/.xml` |

运动仅选关节 0，步幅 5%、容差 2%、5000 ms 到位上限，未启用初始姿态修正。
命令携带 10 关节向量，其他关节保持采样基准；实际反馈到位及整手基准恢复均由查询验证，
不把串口写入或发送终态当作机械到位。结束 CLI 实读角度为 `[254, 127, 254, 254, 254, 254, 0, 170, 249, 112]`，
与本场开始读数一致。
未发送整手 50%/100% 目标，未写速度、限流、清故障或持久化参数；没有新增人工运动观察记录。
关节 7/8 没有作为本场运动轴测试，不能据此将历史 raw/percent 失败或全关节精度问题改记为通过。

### 复现与证据

只读入口（工作区根目录，按实际构建路径及端口调整）：

```powershell
$Read='.\build-windows-l6-clean\examples\Debug\windows_readonly.exe'
$Cli=@('--backend','slcan','--channel','COM11','--mode','classic','--slcan-protocol','canable-fd',
  '--bitrate','1000000','--serial-baud','115200','--serial-timeout-ms','500','--serial-startup-ms','100',
  '--model','l20lite','--side','left','--address','0x28')
& $Read --list @Cli
& $Read --discover @Cli
& $Read --read @Cli
& $Read --manual @Cli
```

硬件用例使用 `HAND_DEVICE_MODEL=l20lite`、`HAND_DEVICE_SIDE=left`、`HAND_DEVICE_ADDRESS=0x28`、
`HAND_DEVICE_BACKEND=slcan`、`HAND_DEVICE_CHANNEL=COM11`、`HAND_DEVICE_SLCAN_PROTOCOL=canable-fd`，
`HAND_DEVICE_BITRATE=1000000`、`HAND_DEVICE_SERIAL_BAUD=115200`、
`HAND_DEVICE_SERIAL_TIMEOUT_MS=500`、`HAND_DEVICE_SERIAL_STARTUP_MS=100`。
先完成上述只读用例，再设置 `HAND_DEVICE_ENABLE_MOTION_TESTS=1`，
以关节 0、5% 步幅、2% 容差、5000 ms 上限运行 `HandDevice hardware raw angle commands`，结束后关闭运动开关。

原始证据及 `run-l20lite-slcan-classic.ps1`（仓库外）：
`acceptance/l20lite-slcan-classic-20261019-141757`，含双仓 SHA/状态/差异、
二进制哈希、串口/驱动/系统、CLI 与用例日志、JUnit、环境变量/退出码、映射配置和 `results.json`。
USB 拔插、PCAN、Release、其他手型/固件及长期负载未在本场测试。

## Windows PCAN Classic 实测（2026-10-19，UTC+08:00）

本场更换为 PCAN-USB，验证 L20 Lite 的 Windows 原生驱动接入及一次关节 0 运动恢复。
14:50:13–14:50:46，CLI 枚举、发现、发现连接、显式连接及运动后再次显式连接均退出码 0；
**6/6 个实机用例、552 条断言通过，0 失败、0 跳过**。本场不重复 Linux 全关节或参数验收，历史关节 7/8 问题保留。

### 环境与设备

- Windows 11 家庭版中文版 x64，`10.0.26200`；MSVC v143 14.44、Windows SDK `10.0.26100.0`、CMake `3.31.8`、Catch2 `3.8.1`，Debug `/MDd`。
- hand：`a6d5a73c4faf844d7ff2a7aa4d996c28dcc6d1eb`，含未提交的文档及 Linux L6 示例整理；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`，工作区干净。本场未修改 SDK 或硬件测试源码，复用本日已验证构建；测试程序、只读入口、两个 SDK 静态库及 PCAN DLL 的 SHA256 与 L20 PCAN 复测场次一致。
- PCAN-USB，USB VID:PID=`0c72:000c`，设备实例 `USB\VID_0C72&PID_000C\5&7D1F3A1&0&6`；PEAK-System 驱动 `5.0.0.20000`，`oem302.inf`。适配器序列号及固件未报告。
- 配置：`pcan / PCAN_USBBUS1`，**Classic CAN** 1 Mbit/s；枚举显示 `FD=no condition=1 owned=0`，不设置 FD 时序或数据速率，不使用 COM 串口。
- x64 DLL：`C:\Windows\System32\PCANBasic.dll`，版本 `5.0.0.1115`，SHA256 `6FCEB8F08837F483925C10F7BCBFA8D6BBF8DF51E10CBC6F161C9DD45D0F8871`。
- 本场身份实读：L20 Lite 左手，地址 `0x28`，序列号设备上报为 `0`，产品码 `LHT10-07-0-L-B-1-A`，固件 `V4.6.10`、硬件 `V1.1.1`、机械 `V0.0.7`，10 关节。发现与显式连接身份一致。
- 供电、接线及终端电阻规格未独立测量；CLI 使用场次隔离配置，Catch2 入口使用构建目录 `tests/config`，未加载用户映射。运行前后映射配置均有副本。

### 运行与结果

| 项目 | 结果 | 原始证据 |
| --- | --- | --- |
| 适配器枚举 / 发现 | PCAN_USBBUS1 可用，发现一个 L20 Lite 左手 Verified 候选，`model=3 side=1 status=0` | `list.log`、`discover.log` |
| 发现连接 / 显式连接 | 身份一致，均读取 10 个角度并正常关闭 | `read.log`、`manual.log` |
| `HandDevice hardware identity refresh and cache` | 1 用例、69 断言通过；身份刷新、缓存及重开一致 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、99 断言通过；10 关节反馈有效、非 stale，连续采样递增 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；关闭、重开及后续反馈正常 | `lifecycle.log/.xml` |
| `HandDevice hardware faults and snapshot projection` | 1 用例、95 断言通过；故障读取、解析及快照投影一致 | `faults.log/.xml` |
| `HandDevice hardware raw angle commands` | 1 用例、150 断言通过；关节 0 `thumb_flex`，基准 raw=254、目标 raw=241；目标反馈及恢复通过，`joints=1/10` | `angle-smoke.log/.xml` |
| 运动后显式连接 / 再次角度读取 | CLI 退出码 0；角度用例 1 个、99 断言通过 | `manual-after.log`、`angles-after.log/.xml` |

运动仅选关节 0，步幅 5%、容差 2%、5000 ms 到位上限，未启用初始姿态修正。
命令携带 10 关节向量，其他关节保持采样基准；角度命令检查发送完成终态，实际到位及整手基准恢复由查询验证。
结束 CLI 实读角度为 `[254, 127, 254, 254, 254, 254, 0, 170, 249, 112]`，与开始读数一致，连接正常关闭。
未发送整手 50%/100% 目标，未写速度、限流、清故障或持久化参数；没有新增人工运动观察记录。
关节 7/8 没有作为本场运动轴测试，不能据此将历史 raw/percent 失败或全关节精度问题改记为通过。

### 复现与证据

只读入口（工作区根目录，按实际构建路径、通道及 DLL 调整）：

```powershell
$Read='.\build-windows-l6-clean\examples\Debug\windows_readonly.exe'
$Cli=@('--backend','pcan','--channel','PCAN_USBBUS1','--mode','classic',
  '--library','C:\Windows\System32\PCANBasic.dll','--bitrate','1000000',
  '--model','l20lite','--side','left','--address','0x28')
& $Read --list @Cli
& $Read --discover @Cli
& $Read --read @Cli
& $Read --manual @Cli
```

硬件用例设置 `HAND_DEVICE_MODEL=l20lite`、`HAND_DEVICE_SIDE=left`、`HAND_DEVICE_ADDRESS=0x28`、
`HAND_DEVICE_BACKEND=pcan`、`HAND_DEVICE_CHANNEL=PCAN_USBBUS1`、
`HAND_DEVICE_LIBRARY=C:\Windows\System32\PCANBasic.dll`、`HAND_DEVICE_BITRATE=1000000`。
先完成上述只读用例，再开启 `HAND_DEVICE_ENABLE_MOTION_TESTS=1`，
以关节 0、5% 步幅、2% 容差、5000 ms 上限运行 `HandDevice hardware raw angle commands`，结束后关闭运动开关。

原始证据及 `run-l20lite-pcan.ps1`（仓库外）：
`acceptance/l20lite-pcan-20261019-144956`，含双仓 SHA/状态/差异、
二进制及 DLL 哈希、适配器/驱动/系统、CLI 与用例日志、JUnit、环境变量/退出码、映射配置和 `results.json`。
USB 拔插、PCAN FD、Release、其他手型/固件及长期负载未在本场测试。
