# L20 实机验收报告

当前结论：**Linux 常用接口实机测试已完成**。最终自动批次 31 项通过、1 项不适用，真实断电恢复另 1 项通过，共 32 项通过；设备偶发故障见下方记录，高级功能不纳入验收。

2026-10-16 Windows / PCAN Classic / Debug 已完成显式连接的身份、16 关节读取、关闭重连及单关节角度收发/恢复，6 个实机用例、756 条断言通过；该场次 CLI 受系统策略阻止的记录保留。

2026-10-19 PCAN Classic 复测已补齐枚举、发现和发现连接，并再次验证读取、重连及单关节运动恢复，**6 个用例、756 条断言通过，无失败或跳过**。此结果不代表完整 Windows 发行包或 Release 验收通过，详见文末。

2026-10-19 已完成 Windows / SLCAN Classic / Debug 的枚举、发现、发现连接、显式连接、16 关节读取及单关节运动恢复。最终 6 个用例、756 条断言通过；首次详细输出模式下的快照过期失败及正常输出模式复测记录均保留，详见文末。

## 环境

- 日期：2026-10-14，UTC+08:00。
- 设备：L20 右手，地址 `0x27`，序列号 `112`，产品码 `LHT20-010-112-R-B-1-C`。
- 固件 `V3.0.6`，PCB `V4.0.1`，机械版本 `V10.2.0`。
- 平台：Ubuntu 24.04.3 LTS x86_64，Linux `7.0.0-38-generic`，GCC 13.3.0。
- 适配器：USB 产品名 `XCAN-USB`，VID:PID `0c72:000c`，内核驱动 `pcan_usb`；SocketCAN `can0`，经典 CAN 1 Mbit/s。
- hand 测试基线：`1f629e4e9f474a068012cef0cf6afd76dbabfd24` 加本轮 L20 修复；对应工单 #196，代码提交 `f89a19b659f1687bade463c38aa5dfe8b4622382`，测试提交 `e18e41818e42f929f555a554f68787528ee99753`。comm：`18f7e0e87f4710fc68af7b66d6cb5cdf97018b96`。
- 原速度、限流配置未知。用户确认所有 16 关节以速度 raw=20、限流 raw=60 建立新基准，结束保留，不保存 EEPROM；这不是恢复原配置。

## 测试与结果

测试通过公开 `HandDevice` 接口执行。用户授权安全环境下运动，按实际设备反馈判定，不要求逐次人工观察。角度步幅 5%、容差 2%（约 5.1 raw）、到位等待 5 秒。

```bash
export HAND_DEVICE_MODEL=l20 HAND_DEVICE_SIDE=right HAND_DEVICE_ADDRESS=0x27 CAN_INTERFACE=can0
export HAND_DEVICE_ENABLE_MOTION_TESTS=1 HAND_DEVICE_ENABLE_FAULT_TESTS=1
export HAND_DEVICE_ENABLE_CONTROL_TESTS=1 HAND_DEVICE_ENABLE_PARAMETER_TESTS=1
export HAND_DEVICE_ANGLE_STEP_PERCENT=5 HAND_DEVICE_ANGLE_TOLERANCE_PERCENT=2 HAND_DEVICE_ANGLE_SETTLE_MS=5000
export HAND_DEVICE_BASELINE_SPEEDS=20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20
export HAND_DEVICE_BASELINE_CURRENT_LIMITS=60,60,60,60,60,60,60,60,60,60,60,60,60,60,60,60
env -u HAND_DEVICE_ANGLE_JOINT ./build/tests/tests '[hardware][device]~[disconnect]~[persistent]' --order decl
```

| 项目 | 结果 |
| --- | --- |
| 身份、生命周期、读取、输入校验、触觉、轮询、快照与事件流 | 只读与角度校验批次 17/17 通过，3840 条断言。 |
| raw / percent 角度控制 | 两种方式均覆盖 16/16 关节，单独用例和综合角度用例通过；每次验证实际到位及测试基准姿态恢复。 |
| 命令回执、目标替换、取消与旧目标不重放 | 自动批次中的适用命令契约通过。L20 没有要求 ACK 的命令，“缺 ACK 超时”用例跳过，记为不适用。 |
| 速度、限流写入与新基准恢复 | 各覆盖 16 通道，验证 `Dispatched` 终态及发送记录；没有配置读回，注明 `transport-receipt-only`，不宣称实测配置值已读回。 |
| 清故障与能力拒绝 | 修复写入间隔后用例通过，检查清故障回执及当次读回全 0。L20 不支持使能/失能，验证明确拒绝。 |
| 真实断连与恢复 | 标准用例通过，34 条断言。手断电后读取失败；同一只手重新上电后 `recover()` 成功，身份一致、连接代次增加、轮询恢复、新鲜快照及后续序号更新。 |

最终自动批次为 32 用例：31 通过、0 失败、1 不适用跳过，13067 条断言全部通过；加上真实断连恢复，共 13101 条断言通过。首次批次清故障失败、首次人工断连等待超时均经修复后复测，本结论采用最终结果。

## 修复与结束状态

- 兼容 L20 序列号最后一帧省略填充，仅接受末片短帧，保留前片长度和重复片冲突检查。
- L20 限流写应答实际是实时电流，移除错误的限流配置读取/快照声明；`get_current_limits()` 明确拒绝，写入仍支持。
- 修正参数测试对有 setter、没有配置 getter 的型号支持，要求已知恢复基准，只验证发送回执。
- 为速度、限流及清故障写入增加帧间隔，避免背靠背发送时部分应答缺失并阻断后续查询；增加清故障后可继续查询的离线回归。
- 相关离线回归 83 用例、715 条断言通过；最终 CTest 4/4 通过。
- 断连复测使用 `HAND_DEVICE_DISCONNECT_WAIT_MS=900000`，每步现场操作等待 15 分钟，通信请求超时不变。运行和标记方式见 [测试说明](../../../tests/hand/device/hardware/README.md)。
- 结束时已发送 16 关节速度 20、限流 60 的新基准，发送成功；设备没有对应配置读回，不宣称读回确认。各次运动均恢复本次起始姿态，断电前的姿态不强制重放。
- 设备两个拇指关节偶发上报 `0x80`（电机通信异常），只读查询也出现；原始 CAN 应答 `59 80 80 00 00 00 00` 与 SDK 解析一致，随后又读到 0。未确定硬件或固件原因，不隐去此现象。
- 最后一轮只读采样 16 关节故障全 0、温度 32–34°C；已停止测试。上述通过结果不代表设备故障永不复现，提交并冻结本轮修复后仍需对应发布版本复验。

原始 CAN 抓包、JUnit 和调试日志保存在仓库外。恢复出厂、参数持久化及高级调参未执行。

## Windows PCAN 适配实测（2026-10-16，UTC+08:00）

本场范围为 Windows 接入与一次单关节角度收发，不重复 Linux 的全关节、参数、故障注入及长期负载验收。
通过标准 `HandDevice` 硬件用例执行，16:53:00–16:53:54 共 **6/6 用例、756 条断言通过**，
0 失败、0 跳过，逐组退出码均为 0。原 Linux 结果及偶发故障记录保留。

### 环境

- Windows 11 家庭版中文版 x64，25H2，`10.0.26200.9445`；VS 2022 17.14.42，MSVC v143 14.44.35207 / `cl 19.44.35229.0`，Windows SDK `10.0.26100.0`，CMake `3.31.8`，Catch2 `3.8.1`。本场为 Debug `/MDd`。
- hand：`5ed296c4dd2b6be2776d78a7c3f7e423333232c3`，含未提交的 Windows 兼容、异常上下文及默认配置目录修复等修改；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`，工作区干净。以证据中的差异及二进制哈希为准，不能仅按 HEAD 复现。
- 本场身份实读：L20 右手，地址 `0x27`，序列号 `112`，产品码 `LHT20-010-112-R-B-1-C`，固件 `V3.0.6`、PCB `V4.0.1`、结构 `V10.2.0`。型号/侧别/地址由连接校验，未仅沿用历史报告。
- PCAN-USB，VID:PID=`0c72:000c`，`PCAN_USBBUS1`，Classic CAN，1 Mbit/s；驱动 `5.0.0.20000`，PEAK-System，`oem302.inf`。适配器序列号/固件未报告，接线/终端电阻及供电规格未独立测量。
- x64 DLL：`C:\Windows\System32\PCANBasic.dll`，版本 `5.0.0.1115`，SHA256 `6FCEB8F08837F483925C10F7BCBFA8D6BBF8DF51E10CBC6F161C9DD45D0F8871`。
- 复用已有 Windows 构建/基础离线结果；默认配置目录修复另有 4 个回归用例、46 条断言通过。本场未重跑全量 CTest；不是 Debug/Release 发行包验证。硬件测试进程使用构建目录内的隔离映射配置。

### 运行与结果

| 分组 / 用例名 | 结果 | 原始证据 |
| --- | --- | --- |
| `HandDevice hardware identity refresh and cache` | 1 用例、69 断言通过；读回上述身份，刷新/缓存、关闭再打开身份一致 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、147 断言通过；16 关节有效、非 stale、有限角度，连续采样时间递增 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；正常关闭、重开、身份固定、新反馈，关闭后无残留轮询/待发命令 | `lifecycle.log/.xml` |
| `HandDevice hardware faults and snapshot projection` | 1 用例、131 断言通过；16 个故障码当次均为 0，快照与读回一致 | `faults.log/.xml`，控制前开启 console success 输出保留实际故障值 |
| `HandDevice hardware raw angle commands` | 1 用例、222 断言通过；`joints=1/16`，关节 0 `thumb_rotation`，基准 raw=128、目标 raw=115；目标反馈及恢复通过 | `angle-smoke.log/.xml` |
| 运动后再次 `HandDevice hardware angle readback` | 1 用例、147 断言通过；重新连接并读回有效的 16 关节角度 | `angles-after-motion.log/.xml` |
| README Windows C++ 只读示例（独立程序，另计） | 使用安装后的 Debug SDK 编译并运行，退出码 0；未设置 HOME/XDG_CONFIG_HOME，成功实读 L20 产品码及 16 个角度并关闭 | `documented-examples.log`、`readme-windows-pcan-run.log` |
| CLI 枚举/发现、发现候选创建连接 | `windows_readonly.exe --list` 在程序启动阶段被系统应用控制策略阻止，未执行后续 CLI 发现/读取；不计入通过用例 | `list-error.txt`、`code-integrity-blocks.json` |

运动设置为关节 0、5% 步幅、2% 容差、5000 ms 到位上限，未启用初始姿态修正。
命令携带 16 关节向量，其他关节保持本场基准目标，测试结束检查恢复。L20 不调用不支持的
使能/失能；角度命令成功终态为发送完成，实际到位及恢复由角度查询确认。
未执行全关节 50% 示例，未写速度、限流、清故障或持久化参数。
本场当次故障码为 0 不否定此前记录的偶发 `0x80`，没有新增人工运动观察记录。

README 示例另行读回产品码 `LHT20-010-112-R-B-1-C`，16 关节 raw 为
`[127, 127, 126, 128, 129, 127, 129, 127, 126, 129, 126, 126, 129, 126, 126, 129]`。
该程序只读，不计入上述六个 Catch2 用例/756 条断言；10 个文档 C++ 示例均通过安装后编译检查。

重现配置与入口（仓库外 `acceptance/run-l20-pcan.ps1`）：

```powershell
$env:HAND_DEVICE_MODEL='l20'; $env:HAND_DEVICE_SIDE='right'; $env:HAND_DEVICE_ADDRESS='0x27'
$env:HAND_DEVICE_BACKEND='pcan'; $env:HAND_DEVICE_CHANNEL='PCAN_USBBUS1'
$env:HAND_DEVICE_LIBRARY='C:\Windows\System32\PCANBasic.dll'; $env:HAND_DEVICE_BITRATE='1000000'
# 先完成上述四个只读用例，再开启一个关节的小幅运动。
$env:HAND_DEVICE_ANGLE_JOINT='0'; $env:HAND_DEVICE_ANGLE_STEP_PERCENT='5'
$env:HAND_DEVICE_ANGLE_TOLERANCE_PERCENT='2'; $env:HAND_DEVICE_ANGLE_SETTLE_MS='5000'
$env:HAND_DEVICE_ENABLE_MOTION_TESTS='1'
try {
  & $Tests 'HandDevice hardware raw angle commands' --order decl --abort `
    --reporter console --reporter "JUnit::out=$Evidence\angle-smoke.xml"
} finally { Remove-Item Env:HAND_DEVICE_ENABLE_MOTION_TESTS -ErrorAction SilentlyContinue }
```

原始证据：`acceptance/l20-pcan-20261016-165107`，包含六组控制台/JUnit、
环境变量与退出码、`results.json`、设备/驱动/系统、双仓 SHA/状态/差异及测试二进制/DLL 哈希。
SLCAN、PCAN FD、USB 拔插、其他构建配置和长期性能未执行；CLI 发现路径仍需在允许程序运行的环境中补测。

## Windows SLCAN Classic 实测（2026-10-19，UTC+08:00）

本场验证 L20 的 Windows 串口接入，不重复 Linux 的全关节、参数、故障注入或长期负载验收。
11:42:32–11:43:40 完成 CLI 检查及硬件用例。**最终 6/6 用例、756 条断言通过，无跳过**；
首次故障快照用例在详细成功输出模式下失败，正常输出模式复测通过，失败证据未覆盖。

### 环境与设备

- Windows 11 家庭版中文版 x64，`10.0.26200`；MSVC v143 14.44、Windows SDK `10.0.26100.0`、CMake `3.31.8`、Catch2 `3.8.1`，Debug `/MDd`。
- hand：`a6d5a73c4faf844d7ff2a7aa4d996c28dcc6d1eb`，含未提交的文档及 Linux L6 示例整理；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`，工作区干净。本场未修改 SDK 或硬件测试源码；运行前重新构建 `tests` 和 `windows_readonly`，源码差异、CMake 缓存及二进制哈希已保存。
- 适配器：COM11，USB VID:PID=`16d0:117e`，USB 序列字符串 `3472396E3335`；Microsoft `usbser.inf`，驱动 `10.0.26100.9278`。具体商业型号及固件版本未报告，与本日 L6 Classic 场次为同一根适配器。
- 配置：`slcan / canable-fd`，**Classic CAN** 1 Mbit/s，UART 115200，串口超时 500 ms，启动等待 100 ms；不设置 FD 数据速率，不使用厂商 CAN DLL，未刷写适配器固件。
- 本场身份实读：L20 右手，地址 `0x27`，序列号 `112`，产品码 `LHT20-010-112-R-B-1-C`，固件 `V3.0.6`、PCB `V4.0.1`、结构 `V10.2.0`，16 关节。
- 供电、接线及终端电阻规格未独立测量；CLI 使用场次隔离配置，Catch2 入口使用构建目录 `tests/config`，未加载用户映射。运行前后映射配置均有副本。

### 运行与结果

| 项目 | 结果 | 原始证据 |
| --- | --- | --- |
| 枚举 / 发现 | COM11 可枚举，发现一个 L20 右手 Verified 候选，`model=4 side=2 status=0` | `list.log`、`discover.log` |
| 发现连接 / 显式连接 | 身份一致，均读取 16 个角度并正常关闭，退出码均为 0 | `read.log`、`manual.log` |
| `HandDevice hardware identity refresh and cache` | 1 用例、69 断言通过；身份刷新、缓存及重开一致 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、147 断言通过；16 关节反馈有效、非 stale，连续采样递增 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；关闭、重开及后续反馈正常 | `lifecycle.log/.xml` |
| `HandDevice hardware faults and snapshot projection` | 最终 1 用例、131 断言通过；首次失败及复测说明见下文 | `faults-retry.log/.xml` |
| `HandDevice hardware raw angle commands` | 1 用例、222 断言通过；关节 0 `thumb_rotation`，基准 raw=254、目标 raw=241；目标反馈及恢复通过，`joints=1/16` | `angle-smoke.log/.xml` |
| 运动后显式连接 / 再次角度读取 | CLI 退出码 0；角度用例 1 个、147 断言通过 | `manual-after.log`、`angles-after.log/.xml` |

首次故障快照检查使用 `--success` 逐条输出成功断言：61 条断言中 60 通过、1 失败，
失败项为快照角度元信息的 `meta.stale`，进程退出码 42。该次实际读到的 16 个故障码均为 0。
测试在获取快照前输出多条成功断言，输出耗时可能使先前角度样本过期；同一二进制、
同一设备配置去掉逐条成功输出后，131 条断言全部通过。未放宽新鲜度阈值或修改 SDK/测试，
原始 `faults.log/.xml`、退出码及最终 `faults-retry.log/.xml` 全部保留。
因此“最终通过”不表示本场所有尝试均无失败，当次故障码为 0 也不否定历史偶发 `0x80` 记录。

运动仅选关节 0，步幅 5%、容差 2%、5000 ms 到位上限，未启用初始姿态修正。
角度命令携带 16 关节向量，其余关节保持采样基准；实际反馈到位及基准恢复均由查询验证。
L20 不调用不支持的使能/失能，角度命令终态为发送完成，不将串口写入完成视作机械到位。
运动后 CLI 实读角度为 `[254, 255, 128, 130, 255, 214, 244, 201, 234, 244, 25, 253, 250, 0, 229, 239]`。
未发送整手 50%/100% 目标，未写速度、限流、清故障或持久化参数；没有新增人工运动观察记录。

### 复现与证据

只读入口（工作区根目录，按实际构建路径及端口调整）：

```powershell
$Read='.\build-windows-l6-clean\examples\Debug\windows_readonly.exe'
$Cli=@('--backend','slcan','--channel','COM11','--mode','classic','--slcan-protocol','canable-fd',
  '--bitrate','1000000','--serial-baud','115200','--serial-timeout-ms','500','--serial-startup-ms','100',
  '--model','l20','--side','right','--address','0x27')
& $Read --list @Cli
& $Read --discover @Cli
& $Read --read @Cli
& $Read --manual @Cli
```

硬件用例复用上述 PCAN 场次的筛选；改为 `HAND_DEVICE_BACKEND=slcan`、
`HAND_DEVICE_CHANNEL=COM11`、`HAND_DEVICE_SLCAN_PROTOCOL=canable-fd`，
`HAND_DEVICE_BITRATE=1000000`、`HAND_DEVICE_SERIAL_BAUD=115200`、
`HAND_DEVICE_SERIAL_TIMEOUT_MS=500`、`HAND_DEVICE_SERIAL_STARTUP_MS=100`，清除 FD 数据速率及 DLL 配置。
运动阶段额外设置 `HAND_DEVICE_ENABLE_MOTION_TESTS=1` 和关节 0、5% 步幅、2% 容差、5000 ms 上限。

原始证据及 `run-l20-slcan-classic.ps1`（仓库外）：
`acceptance/l20-slcan-classic-20261019-113822`，含双仓 SHA/状态/差异、
构建与二进制哈希、串口/驱动/系统、CLI 与用例日志、JUnit、环境变量/退出码、映射配置和 `results.json`。
该结果补充 L20 的 SLCAN Classic 适配记录，不替代 PCAN 发现补测；USB 拔插、Release、
其他手型/固件及长期负载均未在本场测试。

## Windows PCAN Classic 复测（2026-10-19，UTC+08:00）

本场补齐 2026-10-16 缺少的 PCAN 发现路径，并复测显式连接、16 关节读取、关闭重连及单关节角度收发恢复。
14:45:46–14:46:14，CLI 枚举、发现、发现连接、显式连接及运动后再次显式连接均退出码 0；
**6/6 个实机用例、756 条断言通过，0 失败、0 跳过**。此前系统策略受阻及设备偶发故障记录保留。

### 环境与设备

- Windows 11 家庭版中文版 x64，`10.0.26200`；MSVC v143 14.44、Windows SDK `10.0.26100.0`、CMake `3.31.8`、Catch2 `3.8.1`，Debug `/MDd`。
- hand：`a6d5a73c4faf844d7ff2a7aa4d996c28dcc6d1eb`，含未提交的文档及 Linux L6 示例整理；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`，工作区干净。本场未修改 SDK 或硬件测试源码，复用本日已验证构建；测试程序、只读入口、两个 SDK 静态库及 PCAN DLL 的 SHA256 与 O6 PCAN 场次一致。
- PCAN-USB，USB VID:PID=`0c72:000c`，设备实例 `USB\VID_0C72&PID_000C\5&7D1F3A1&0&6`；PEAK-System 驱动 `5.0.0.20000`，`oem302.inf`。适配器序列号及固件未报告。
- 配置：`pcan / PCAN_USBBUS1`，**Classic CAN** 1 Mbit/s；枚举显示 `FD=no condition=1 owned=0`，不设置 FD 时序或数据速率，不使用 COM 串口。
- x64 DLL：`C:\Windows\System32\PCANBasic.dll`，版本 `5.0.0.1115`，SHA256 `6FCEB8F08837F483925C10F7BCBFA8D6BBF8DF51E10CBC6F161C9DD45D0F8871`。
- 本场身份实读：L20 右手，地址 `0x27`，序列号 `112`，产品码 `LHT20-010-112-R-B-1-C`，固件 `V3.0.6`、PCB `V4.0.1`、机械 `V10.2.0`，16 关节。发现与显式连接身份一致。
- 供电、接线及终端电阻规格未独立测量；CLI 使用场次隔离配置，Catch2 入口使用构建目录 `tests/config`，未加载用户映射。运行前后映射配置均有副本。

### 运行与结果

| 项目 | 结果 | 原始证据 |
| --- | --- | --- |
| 适配器枚举 / 发现 | PCAN_USBBUS1 可用，发现一个 L20 右手 Verified 候选，`model=4 side=2 status=0` | `list.log`、`discover.log` |
| 发现连接 / 显式连接 | 身份一致，均读取 16 个角度并正常关闭 | `read.log`、`manual.log` |
| `HandDevice hardware identity refresh and cache` | 1 用例、69 断言通过；身份刷新、缓存及重开一致 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、147 断言通过；16 关节反馈有效、非 stale，连续采样递增 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；关闭、重开及后续反馈正常 | `lifecycle.log/.xml` |
| `HandDevice hardware faults and snapshot projection` | 1 用例、131 断言通过；故障读取、解析及快照投影一致 | `faults.log/.xml` |
| `HandDevice hardware raw angle commands` | 1 用例、222 断言通过；关节 0 `thumb_rotation`，基准 raw=254、目标 raw=241；目标反馈及恢复通过，`joints=1/16` | `angle-smoke.log/.xml` |
| 运动后显式连接 / 再次角度读取 | CLI 退出码 0；角度用例 1 个、147 断言通过 | `manual-after.log`、`angles-after.log/.xml` |

运动仅选关节 0，步幅 5%、容差 2%、5000 ms 到位上限，未启用初始姿态修正。
命令携带 16 关节向量，其他关节保持采样基准；角度命令检查发送完成终态，实际到位及恢复由查询验证。
结束 CLI 实读角度为 `[254, 183, 128, 130, 255, 211, 244, 199, 234, 247, 27, 251, 251, 6, 229, 252]`，
恢复检查通过，连接正常关闭。未发送整手 50%/100% 目标，未写速度、限流、清故障或持久化参数。
本场故障用例验证读取、解析及快照一致性，不代表历史偶发 `0x80` 问题已修复；没有新增人工运动观察记录。

### 复现与证据

只读入口（工作区根目录，按实际构建路径、通道及 DLL 调整）：

```powershell
$Read='.\build-windows-l6-clean\examples\Debug\windows_readonly.exe'
$Cli=@('--backend','pcan','--channel','PCAN_USBBUS1','--mode','classic',
  '--library','C:\Windows\System32\PCANBasic.dll','--bitrate','1000000',
  '--model','l20','--side','right','--address','0x27')
& $Read --list @Cli
& $Read --discover @Cli
& $Read --read @Cli
& $Read --manual @Cli
```

硬件用例设置 `HAND_DEVICE_MODEL=l20`、`HAND_DEVICE_SIDE=right`、`HAND_DEVICE_ADDRESS=0x27`、
`HAND_DEVICE_BACKEND=pcan`、`HAND_DEVICE_CHANNEL=PCAN_USBBUS1`、
`HAND_DEVICE_LIBRARY=C:\Windows\System32\PCANBasic.dll`、`HAND_DEVICE_BITRATE=1000000`。
先完成上述只读用例，再开启 `HAND_DEVICE_ENABLE_MOTION_TESTS=1`，
以关节 0、5% 步幅、2% 容差、5000 ms 上限运行 `HandDevice hardware raw angle commands`，结束后关闭运动开关。

原始证据及 `run-l20-pcan-retest.ps1`（仓库外）：
`acceptance/l20-pcan-20261019-144526`，含双仓 SHA/状态/差异、
二进制及 DLL 哈希、适配器/驱动/系统、CLI 与用例日志、JUnit、环境变量/退出码、映射配置和 `results.json`。
该场补齐 L20 的 PCAN Classic 发现及发现连接检查；USB 拔插、PCAN FD、Release、其他固件及长期负载未在本场测试。
