# L6 实机验收报告

当前结论：Linux 常用功能实机验收完成，34 个用例通过，7038 条断言；该场最终运行均无失败、无跳过，退出码 0。Windows / PCAN Classic 已以 Debug 构建完成连通/读取及单关节角度收发/恢复（1 个运动用例、102 条断言，无失败/跳过）。2026-10-19 已在干净源码上补齐此前受系统策略阻止的 Release 离线项，并通过 Release 工程及发行包验收；Release 实机未在本场验证。高级功能不纳入本轮验收。

2026-10-16 SLCAN 离线检查为 Windows Debug 25/25 通过，Release 核心 19/19 通过、6 项 CLI 受系统策略阻塞。2026-10-19 已补充 COM11 / SLCAN Classic / Debug 实测：发现/显式连接、读取和单关节角度收发及恢复均通过，5 个用例、345 条断言，无失败或跳过，结果详见文末。

## 环境

- 日期：2026-10-13，UTC+08:00；角度验收完成于 11:29，其余验收及结束检查完成于 11:57。
- 设备：L6 右手，地址 `0x27`；序列号 `632`，产品码 `LHL6-03.3-632-R-B-1-D`。
- 版本：固件 `V2.4.5`，硬件 `V1.0.0`，结构 `V3.2.0`。
- 平台：Ubuntu 24.04.3 LTS x86_64，Linux `7.0.0-38-generic`，GCC 13.3.0。
- 适配器：`XCAN-USB`，VID:PID=`0c72:000c`，内核驱动 `peak_usb`；`can0`，经典 CAN，1 Mbit/s。
- hand HEAD：`58a0a46a06699cb3791ddc7c627c11b5f90dd774`，工作区含未提交修改，包括 L6 序列号末片长度兼容及参数验收修正。
- comm：`18f7e0e87f4710fc68af7b66d6cb5cdf97018b96`。

## 运行与结果

通过公开 `HandDevice` 接口执行，在 SDK 根目录分组运行：

```bash
cmake --build build --target tests --parallel 4
export HAND_DEVICE_MODEL=l6 HAND_DEVICE_SIDE=right HAND_DEVICE_ADDRESS=0x27 CAN_INTERFACE=can0
unset HAND_DEVICE_HOST_ADDRESS HAND_DEVICE_ANGLE_JOINT HAND_DEVICE_ANGLE_NORMALIZE_START
unset HAND_DEVICE_ENABLE_SAVE_TESTS HAND_DEVICE_ENABLE_TORQUE_TESTS
export HAND_DEVICE_ENABLE_MOTION_TESTS=1 HAND_DEVICE_ENABLE_FAULT_TESTS=1 HAND_DEVICE_ENABLE_CONTROL_TESTS=1
export HAND_DEVICE_ENABLE_PARAMETER_TESTS=1 HAND_DEVICE_BASELINE_SPEEDS=20,20,20,20,20,20
export HAND_DEVICE_ANGLE_STEP_PERCENT=5 HAND_DEVICE_ANGLE_TOLERANCE_PERCENT=2 HAND_DEVICE_ANGLE_SETTLE_MS=5000
./build/tests/tests "[hardware][device][angles]" --order decl
./build/tests/tests "[hardware][device]~[angles]~[parameters]~[manual]~[persistent]" --order decl
./build/tests/tests "HandDevice hardware numeric input validation"
./build/tests/tests "[hardware][device][parameters][write]" --section set_speeds --section set_current_limits --order decl
```

真实断连用例按 [实机测试说明](../../../tests/hand/device/hardware/README.md) 的人工标记流程运行，标记文件放在仓库外。

| 分组 | 结果 |
| --- | --- |
| 角度文件全部用例 | 6/6 通过，1762 条断言；独立与综合 raw / percent 均覆盖 6/6 关节，目标到位及恢复通过 |
| 其余自动验收 | 27/27 通过，5245 条断言；身份、生命周期、构造、触觉与状态读取、输入校验、轮询、事件流、命令替换/失败/超时、不重放、清故障、常用参数及缓存恢复 |
| 真实断连与恢复 | 1/1 通过，31 条断言；用户实际断开并接回连接，读取失败、同一设备身份、新代反馈及轮询恢复均确认 |
| 人工观察 | 用户确认六个关节对应运动正常、无卡顿，角度验收结束恢复 |

运动步幅为范围的 5%，容差为 2%，到位等待上限为 5 秒；未触发初始姿态修正。
L6 不支持的使能、失能检查为明确拒绝，不代表实际执行了这些操作。

参数写入只验收速度、电流限制，各覆盖 6/6 通道并恢复。原速度无法主动查询，用户确认建立六关节 raw=20 新基准，结束保留 20；电流限制恢复设备原值 255。速度通过实际设备写回复确认，不能称为独立查询；电流限制通过主动查询确认。

初测因 V2.4.5 的取整及写入前值回复失败。实测确认速度与电流限制存在 1 raw 差异后，修正参数测试为至少 3 raw 步幅、1 raw 容差；对不能主动查询的配置，在成功完成写入后追加一次相同写入收集确认回复。复测通过，未修改 SDK 返回的实际数值。

结束检查：序列号仍为 632，六关节速度确认回复为 20、电流限制查询为 255、力矩模式为 0、故障码为 0；can0 为 UP / ERROR-ACTIVE，错误计数均为 0（重连后计数重新开始）。

未执行参数持久化、增益/堵转配置及力矩输出等高级功能的实际写入；没有保存 EEPROM。实际发布源码冻结后仍需复测；原始日志保存在仓库外。

## Windows 适配实测（2026-10-16，UTC+08:00）

结论：**L6 右手 / PCAN Classic 的 Windows Debug 连通/读取和单关节角度收发通过**。
Debug 离线 CTest 143/143 通过；Release 构建成功，但离线检查 135/143 通过、8 项因系统策略
无法启动，退出码 8，不能标记 Release 或完整 Windows 构建验收通过。仅在已通过离线检查的
Debug 配置上进行本场实机检查；没有生成代表双配置均通过的 `offline-passed.txt`。
本场复用工作区已有工具、构建与兼容修复，未重复 Linux 通用功能验收。

### 环境与版本

- 系统：原生 Windows 11 家庭版中文版 x64，25H2，`10.0.26200.9445`。
- 工具：VS 2022 Build Tools 17.14.42，v143 14.44.35207 / `cl 19.44.35229.0`，Windows SDK `10.0.26100.0`，CMake `3.31.8`，Catch2 `3.8.1`。本场实机使用 Debug `/MDd`，Release `/MD` 仅构建及部分离线检查。
- hand：`5ed296c4dd2b6be2776d78a7c3f7e423333232c3`，含未提交的 MSVC 编译兼容、异常上下文及离线测试适配修改；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`，工作区干净。实机时源码差异保存在 `source-at-hardware.diff`，不能仅按 HEAD 复现。
- 适配器：枚举为 `PCAN-USB`，VID:PID=`0c72:000c`，`PCAN_USBBUS1`，`FD=no`；首次枚举 `condition=1`、`owned=0`。适配器序列号/固件未报告。驱动 `5.0.0.20000`，PEAK-System，`oem302.inf`。
- DLL：`C:\Windows\System32\PCANBasic.dll`，文件版本 `5.0.0.1115`，SHA256 `6FCEB8F08837F483925C10F7BCBFA8D6BBF8DF51E10CBC6F161C9DD45D0F8871`。Classic CAN，1 Mbit/s；FD 数据速率/时序不适用。
- 本场设备实读：L6 右手，发现地址十进制 `39` / `0x27`，序列号 `632`，产品码 `LHL6-03.3-632-R-B-1-D`，固件 `V2.4.5`、硬件 `V1.0.0`、结构 `V3.2.0`。接线、终端电阻及供电规格未独立测量。

### 运行与结果

| 项目 | 本场结果与证据 |
| --- | --- |
| Debug 构建及离线检查 | `build-Debug-153310.log`、`offline-Debug-153310.log/.xml`：CTest 143/143 通过，退出码 0；底层 comm 用例存在 7 个平台跳过（1 个符号链接权限、6 个 POSIX 子进程），不能称全部 Catch2 用例无跳过 |
| Release 构建及离线复查 | `build-Release-153310.log` 构建成功；`offline-Release-155056.log/.xml`：135 项通过，8 个 `windows_readonly_*` 因程序不能启动而 BAD_COMMAND；早次异步探针启动也被阻止，本次该探针已通过 |
| 枚举/发现 | `list-Debug.log`、`discover-Debug.log`：发现 PCAN_USBBUS1 上的 L6 右手，退出码均为 0 |
| 完整身份与发现地址 | `identity-Debug.log`：使用公开 discovery / HandDevice 接口的辅助探针实读上述身份、6 个有效且非 stale 的有限角度，显式关闭成功，退出码 0；探针源码为仓库外的 `acceptance/l6-identity-probe.cpp` |
| 发现连接读取 | `read-Debug.log`：身份正确，六角度 `[83, 188, 213, 209, 123, 121]`，退出码 0 |
| 显式连接读取 | `manual-Debug.log`：同一产品/固件，六角度 `[83, 188, 213, 209, 123, 121]`，退出码 0 |
| 单关节 raw 角度及恢复 | 15:53:18，`angle-smoke-Debug.log/.xml`：`HandDevice hardware raw angle commands` 1/1 通过，102 条断言，0 失败、0 跳过，退出码 0；`joints=1/6`，关节 0 `thumb_flex`，基准 raw=83、目标 raw=96，目标反馈及恢复检查通过 |
| 运动后重新连接/读取 | `manual-after-motion-Debug.log`：同一身份，六角度 `[84, 189, 212, 208, 122, 120]`，退出码 0；连续独立进程均能重新打开并正常关闭，无清理错误或上次占用阻塞 |
| SLCAN Classic、真实 USB 拔插恢复 | 未执行；没有人工运动观察记录，不据此补写人工观察通过 |

运动参数：仅关节 0、5% 步幅、2% 容差、5000 ms 到位上限，未启用初始姿态修正。
用例命令携带六关节目标向量，其他关节保持基准目标；恢复检查通过且运动后重新读取仍接近基准。
未写入速度、限流或持久化参数；L6 未调用不支持的使能/失能接口。

复用/执行脚本位于仓库外 `acceptance/continue-l6-debug.ps1`，只读阶段之后以 `-Motion`
运行上述一个运动用例，日志不覆盖已有失败场次。核心真机入口为：

```powershell
# 在 SDK 工作区，配置见 hardware-config-Debug.json / angle-smoke-Debug-env.txt
powershell.exe -NoProfile -ExecutionPolicy Bypass -File acceptance/continue-l6-debug.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File acceptance/continue-l6-debug.ps1 -Motion
```

Release 的系统阻塞证据为 `code-integrity-blocks.json`：Code Integrity 事件 3077，
`windows_readonly.exe` 不满足签名级别或代码完整性策略，策略 ID
`{0283ac0f-fff1-49ae-ada1-8a933130cad6}`。这属于程序启动被系统策略阻止，不能记为硬件通信失败；
后续需在允许这些构建产物运行的受支持环境中完成 Release 离线检查及相应实机验证。

原始证据目录：`acceptance/l6-pcan-20261016-151627`。
包含双仓 SHA/状态/差异、环境与驱动、DLL/二进制哈希、各步骤控制台/退出码及 JUnit。
Windows 结论仅覆盖本场 L6 / PCAN Classic / Debug 的连通和一次单关节收发，不扩展到
Release、SLCAN、USB 恢复、其他手型或长期性能。

## SLCAN 离线检查（2026-10-16，UTC+08:00）

2026-10-16 该场次未接入 SLCAN 适配器，按当时要求只做离线测试。结果：**Windows Debug 离线检查 25/25 通过，0 失败、0 跳过**；
Release 的通信层及 HandDevice 配置/发现逻辑 19/19 通过，0 失败、0 跳过，6 项 Release CLI
检查因已有 Windows 应用控制策略无法启动，不能标记通过。本轮不再等待 SLCAN 硬件；实机连接、
真实串口收发、运动及 USB 拔插均未执行，离线结论不表示 SLCAN 实机验收通过。

本场复用已有 Debug/Release 产物，未改动 SDK 源码。hand/comm SHA 与上场相同；源码差异、
二进制哈希及运行脚本均随场次保存。

| 离线分组 | Debug | Release | 覆盖 |
| --- | --- | --- | --- |
| comm `[slcan]` | 14 用例、11235 断言通过，退出码 0 | 14 用例、11235 断言通过，退出码 0 | Classic/FD 编解码、全部 CAN 速率与 UART 独立配置、Lawicel ACK、CANable 无 ACK、分段/合并帧、队列边界、非法 ASCII、超时/断连/写入失败、取消及关闭/清理重试；Windows 不存在串口的系统错误诊断 |
| hand SLCAN 配置与发现 | 5 用例、69 断言通过，退出码 0 | 5 用例、69 断言通过，退出码 0 | Classic/CANable FD 配置校验、明确端口允许列表、无权限时不打开串口、错误配置在打开前拒绝、注入适配器共享身份发现逻辑 |
| CLI SLCAN 参数检查 | CTest 6/6 通过，退出码 0 | 6 项 BAD_COMMAND，退出码 8；程序未启动 | FD/profile 不匹配、缺少允许端口、非法 UART 波特率/FD 速率/协议/启动延迟；均应在真实 I/O 前拒绝 |

通信层使用注入串口模拟器，发现逻辑使用注入适配器；不存在串口的诊断用例只尝试已确认不存在的
`COM65535`。Linux PTY 原生串口用例未编入本 Windows 产物，不计入通过数量。CLI 的六项用例
在 CMake 中设置 `WILL_FAIL`，通过表示按预期拒绝非法参数，不表示发现设备成功。

执行入口（仓库外）：`acceptance/run-slcan-offline.ps1`。筛选及报告命令为：

```powershell
# 每个配置分别执行，$Comm / $Tests / $Build / $Evidence 为该场次的绝对路径
& $Comm '[slcan]' --order decl --reporter console --reporter "JUnit::out=$Evidence\comm-$Configuration.xml"
& $Tests '[slcan],*SLCAN*,adapter discovery sources share the identity probe engine' --order decl `
  --reporter console --reporter "JUnit::out=$Evidence\hand-$Configuration.xml"
ctest --test-dir $Build -C $Configuration -R '^windows_readonly_slcan_' `
  --output-on-failure --no-tests=error --output-junit "$Evidence\cli-$Configuration.xml"
```

最终证据：`acceptance/slcan-offline-20261016-160434`。
保存六组控制台/JUnit/退出码、`results.json`、双仓版本/状态/差异和二进制 SHA256。
同目录 `offline-Debug-passed.txt` 仅证明 Debug 三组均通过；没有生成代表双配置完整通过的标记。
汇总脚本因 Release CLI 受阻返回 1，保留该限制，不将其隐藏为整体成功。

## Windows SLCAN Classic 实测（2026-10-19，UTC+08:00）

本场以 SLCAN 适配器连接 L6，在读取真实身份后执行一次单关节小幅运动及恢复。
**5/5 个实机用例、345 条断言通过，0 失败、0 跳过**；CLI 枚举、发现、发现连接、显式连接及
运动后再次显式连接均退出码 0。该结果补充了上述离线场次，未重跑 Linux 通用功能验收。

### 环境与设备

- Windows 11 家庭版中文版 x64，25H2，`10.0.26200.9445`；MSVC v143 14.44，Debug `/MDd`，复用已验证构建，不因换手重跑全部离线检查。
- hand：`a6d5a73c4faf844d7ff2a7aa4d996c28dcc6d1eb`；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`。场次开始时两仓工作区干净；本场未改 SDK 或硬件测试源码，二进制哈希已保存。
- 适配器：COM11，USB VID:PID=`16d0:117e`，USB 序列字符串 `3472396E3335`；Microsoft `usbser.inf`，驱动 `10.0.26100.9278`。具体商业型号与固件版本未报告。
- 配置：`slcan / canable-fd`，**Classic CAN**，1 Mbit/s；UART 115200，串口超时 500 ms，启动等待 100 ms。未设置 FD 数据速率或厂商 CAN DLL，未刷写适配器固件。
- 本场身份实读：L6 右手，地址 `0x27`，序列号 `632`，产品码 `LHL6-03.3-632-R-B-1-D`，固件 `V2.4.5`、硬件 `V1.0.0`、结构 `V3.2.0`，6 关节。
- 供电、接线与终端电阻规格未独立测量；测试进程使用本场隔离映射配置。

### 运行与结果

| 项目 | 结果 | 原始证据 |
| --- | --- | --- |
| COM 枚举 / 发现 | 明确允许的 COM11 可枚举，发现一个 L6 右手 Verified 候选，`model=1 side=2 status=0` | `list.log`、`discover.log` |
| 发现连接 / 显式连接 | 身份一致、均读取 6 个角度并正常关闭 | `read.log`、`manual.log` |
| `HandDevice hardware identity refresh and cache` | 1 用例、69 断言通过 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、67 断言通过；6 关节反馈有效、非 stale 且采样时间递增 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；关闭、重开、身份和新反馈正常 | `lifecycle.log/.xml` |
| `HandDevice hardware raw angle commands` | 1 用例、102 断言通过；关节 0 `thumb_flex`，采样基准 raw=237、目标 raw=224；目标反馈及恢复通过，`joints=1/6` | `angle-smoke.log/.xml` |
| 结束后显式连接 / 角度读取 | CLI 正常退出；角度用例 1 个、67 断言通过 | `manual-after.log`、`angles-after.log/.xml` |

运动设置为关节 0、5% 步幅、2% 容差、5000 ms 上限，未启用初始姿态修正。
命令携带整手向量，其他关节保持本场采样基准，结束检查恢复；再次显式连接的角度为
`[238, 119, 127, 124, 124, 125]`。未运行整手 50% 示例，未写速度、限流、使能或持久化参数。
没有新增人工运动观察记录，不据此声明所有关节或长期性能已重新验收。

### 固件配置与复用说明

此次实测说明兼容所选 CANable FD 配置的串口适配器可以连接 Classic CAN 的 L6：保留
`canable-fd` 协议，选择 `classic` 模式即可，不必改成等待串口 ACK 的 Lawicel 配置。
本场通过 L6 的实际身份、反馈及运动恢复判断通信成功，不把串口字节写完当作设备执行成功。
未单独查询固件版本或复验本根的 FD 模式，不能由本场推导 Lawicel 固件或其他型号全部通过。

本根 USB 序列字符串与 2026-10-16 L30 SLCAN FD 读取场次相同；与 2026-10-19 O30 SLCAN FD
使用的 COM10 / `3477397D3335` 不同。记录分别对应各场次和配置，不把两根适配器混为同一设备。

只读复现（从工作区根目录执行，替换实际端口/侧别/地址）：

```powershell
$Read='.\build-windows-l6-clean\examples\Debug\windows_readonly.exe'
$Cli=@('--backend','slcan','--channel','COM11','--mode','classic','--slcan-protocol','canable-fd',
  '--bitrate','1000000','--serial-baud','115200','--serial-timeout-ms','500','--serial-startup-ms','100',
  '--model','l6','--side','right','--address','0x27')
& $Read --list @Cli
& $Read --discover @Cli
& $Read --read @Cli
& $Read --manual @Cli
```

原始证据及 `run-l6-slcan-classic.ps1` 副本（仓库外）：
`acceptance/l6-slcan-classic-20261019-105800`，包含双仓 SHA/状态、
二进制哈希、串口/驱动/系统、CLI/用例控制台、JUnit、环境变量/退出码、结果及摘要。
USB 拔插、Release、其他手型与长期负载未在本场测试。
