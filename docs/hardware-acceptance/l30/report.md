# L30 实机验收报告

当前结论：Linux 场次 35 个实机用例按功能分别执行，34 个通过，1 个用例按用户要求跳过。
2026-10-16 Windows / SLCAN FD / Debug 显式连接、身份与 17 关节读取通过；
该场运动因初始姿态越界受阻，CLI 枚举/发现因系统应用控制策略受阻，历史记录保留。
2026-10-19 同一根 COM11 适配器已完成枚举、发现、发现/显式连接、17 关节读取及单关节运动恢复，
**6 个用例、791 条断言通过，无失败或跳过**；使能与结束失能均成功，未启用初始姿态修正，详见文末。

## 环境

- 日期：2026-10-12，UTC+08:00；自动测试完成于 17:04，随后完成真实断连复测及结束检查。
- 设备：L30 右手，NodeID=1、HostID=0；序列号 `3`，产品码 `LHT30-06-003-R-B-3-G`。
- 版本：固件、硬件、结构均为 `V6.1.6`。
- 平台：Ubuntu 24.04.3 LTS x86_64，Linux `7.0.0-38-generic`，GCC 13.3.0。
- 适配器：`candleLightFD gs_usb`，VID:PID=`1d50:606f`，驱动 `gs_usb`。
- 总线：`can0`，CAN FD，仲裁 1 Mbit/s、数据 5 Mbit/s；自动测试前错误计数为 0。
- hand HEAD：`1650f1e3c88c94801f6a1b61bb3323d05dd1d88d`，含本次未提交修复。
- comm：冻结源码 `18f7e0e87f4710fc68af7b66d6cb5cdf97018b96`。
- 测试通过公开 `HandDevice`；构建和操作说明见 [测试 README](../../../tests/hand/device/hardware/README.md)。

## 执行结果

| 分组 | 结果 |
| --- | --- |
| 角度文件全部用例 | 6/6 通过，9246 断言；raw、percent 均覆盖 17/17 关节，独立与综合用例均执行 |
| 其余自动用例 | 27/27 通过，7017 断言；状态、身份、力字段、输入拒绝、轮询、事件流、参数修改/恢复、缓存失效、命令失败/替换/超时、不重放 |
| 真实断连与重连 | 1/1 通过，31 断言；真实 USB 拔插后，读取失败、同一设备身份、新代反馈和轮询恢复均确认 |
| 未执行项 | 1 个用例 SKIP，未计入通过数量 |
| 离线回归 | 874 个用例全部通过 |

通过的 34 个实机用例合计 16294 个断言；跳过项不计为成功。
用户现场确认对应关节运动正常、无卡顿，最后恢复姿态。
本轮角度测试未触发初始越界修正；步幅为范围的 5%，容差为 2%，恢复后失能。
结束检查再次收到失能 ACK，角度读取正常。
结束时 `can0` 为 UP / ERROR-ACTIVE，CAN FD 配置正确，错误计数为 0；
该计数从 USB 重枚举后重新开始。

首次断连恢复因 USB 重新枚举后 `can0` 为 DOWN 而失败；当前工具缺管理员权限，
用户重配 CAN FD 并启用接口。随后重新完成整套真实拔插用例，复测通过。

L30 不支持的加速度、力矩、增益、清故障、保存参数等入口均检查
`UnsupportedOperationError`；其中 `save_parameters()` 未执行 EEPROM 保存。
速度和电流限制各 17 个通道逐个修改并恢复，另检查恢复后旧代 SDK 缓存失效。
协议不提供配置读回；原配置未记录，本次明确建立新基准：所有关节速度 raw=20、
电流限制 raw=60。结束时下发该基准，验证范围为发送终态与 SDK 缓存，
不能据此声称独立读回了硬件配置或恢复了未知原值。

## 本轮软件修复

- 仅配置构造的 SocketCAN 连接保留传输工厂，三种公开构造路径的 `recover()` 均通过。
- 事件流与快照共享元信息补齐逻辑，连接代次、序号及新鲜度检查通过；离线回归增加对应断言。

提交并冻结实际发布源码后还需复测，不能将本工作树结果当作冻结版本验收。

## Windows 适配检查：2026-10-16 / SLCAN FD / Debug

本场按 [Windows 真机指南](../windows.md) 验证实际串口路径，保留上述 Linux 历史结果。
结论为“显式连接/读取通过”；单关节角度收发和发现连接尚未通过，不能标为完整 Windows 适配通过。

### 环境与设备

- 日期/执行：2026-10-16，UTC+08:00，17:45 起；Codex 自动执行，未新增人工运动观察记录。
- 平台：Windows 11 家庭版中文版 x64，版本 `10.0.26200`；Visual Studio 2022 / MSVC v143 14.44，Debug。
- hand SHA：`9f6c23cbae69e02aac01780ce7084ebb2062d328`；comm SHA：`e1a16ad904e501915f85d1fb71db0900e5ceb285`。
  场次开始时两仓均干净；本次仅追加验收文档，未修改 SDK 或测试源码。
- 实读设备：L30 右手，NodeID=`1`、HostID=`0`，17 关节；产品码 `LHT30-06-003-R-B-3-G`，序列号 `3`，
  固件/硬件/结构均为 `V6.1.6`。侧别先由身份查询确认，再显式设置为 right。
- 适配器：USB 串口 `COM11`，VID:PID=`16D0:117E`，USB 序列字符串 `3472396E3335`；
  实际使用 `slcan / canable-fd` 完成 L30 扩展 CAN FD 查询，确认该协议配置可用。
  具体商业型号、固件版本未报告，不仅凭 USB 名称认定固件版本。
- 驱动：Microsoft `usbser.inf`，版本 `10.0.26100.9278`；SLCAN 不加载厂商 CAN DLL。
- CAN：扩展 CAN FD，仲裁 1 Mbit/s、数据 5 Mbit/s；UART 115200，串口超时 500 ms、启动等待 100 ms。
  供电电压、接线细节、终端电阻未报告；CAN 错误计数不可观测。
- 构建：`build-windows-l6-clean` 中的 Debug `tests.exe` 与 `windows_readonly.exe`，
  SHA256 与既有 `windows-config-path-20261016-163936/verified-binary-hashes.json` 一致。
  沿用既有 Debug 离线检查及后续配置路径回归，不因换手重跑；既有 Release CLI 失败不计通过，未做 Release 实机检查。

### 本场结果

共执行 7 次 Catch2 实机测试：6 次通过（624 条断言），1 次初始姿态检查失败
（156 条断言中 155 条通过、1 条失败，退出码 42）；无跳过。
身份探测与显式右手核验为同一个用例的两次执行，角度读取同样执行两次，不计为不同功能用例。

| 检查 / 筛选 | 结果 | 证据 |
| --- | --- | --- |
| `HandDevice hardware identity refresh and cache`，首次不预设侧别 | 1 次通过，68 断言，退出码 0；实读右手、NodeID 1 及完整身份 | `identity-probe.log/.xml` |
| 同一身份用例，显式 right | 1 次通过，69 断言，退出码 0 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 次通过，155 断言，退出码 0；两次有效的 17 关节反馈与新鲜度检查 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 次通过，40 断言，退出码 0；关闭/重开与连接代次检查 | `lifecycle.log/.xml` |
| `HandDevice hardware raw angle commands` | 初始姿态检查失败，156 断言中 1 条失败，退出码 42；未使能、未下发角度目标 | `angle-smoke.log/.xml` |
| 运动中止后角度读取，同上读取用例 | 1 次通过，155 断言，退出码 0 | `angles-after-abort.log/.xml` |
| `HandDevice hardware faults and snapshot projection` | 1 次通过，137 断言，退出码 0；读取故障字段与快照投影一致 | `faults-after-abort.log/.xml` |
| CLI `--list / --discover / --read / --manual` | 均在进程启动前被 Windows Application Control 阻止；无进程退出码，不计测试通过 | 各动作 `*-error.txt`、`*-status.json`，`code-integrity-blocks.json`（事件 3077） |

### 运动受阻原因与剩余项

本次计划只运动关节 0，步幅 5%、容差 2%、到位上限 5000 ms；未开启初始姿态修正。
测试在创建运动恢复守卫及使能前发现以下初始反馈越界：

| 关节索引 / 名称 | 实读 raw | 命令范围 | 回到范围所需修正 | 默认 20% 修正上限 |
| --- | --- | --- | --- | --- |
| 6 / ring_root | -97 | [0, 1600] | 97 | 320 |
| 7 / middle_root | -181 | [0, 1600] | 181 | 320 |
| 9 / pinky_root | -353 | [0, 1600] | 353 | 320 |
| 13 / index_abd | 234 | [-200, 200] | 34 | 80 |
| 14 / index_root | -58 | [0, 1600] | 58 | 320 |

小指根部修正量超过默认上限，不能只打开 `HAND_DEVICE_ANGLE_NORMALIZE_START=1` 就当作轻微越界继续。
没有放宽范围、修正上限或反馈容差，没有调用清故障、速度/限流写入或参数持久化。
本场未发送运动目标，因此没有可报告的目标到位、姿态恢复或结束失能结果。
结束只读检查通过，连接由测试夹具关闭。

补齐运动项前，应先现场检查原姿态、机械状态，使反馈回到合法命令范围；
若需主动修正初始姿态，应根据实际硬件情况单独确定动作范围，保留本次失败证据并另命名重跑。
CLI 发现及通过候选创建连接需要在系统策略允许程序运行的环境中补测；本场未改动系统应用控制策略。
USB 拔插恢复未执行；PCAN FD、MiniCANFD、其他构建配置、全关节控制及长期负载未在本场验证。

重现入口为仓库外 `acceptance/run-l30-slcan.ps1`，按 `Init`、`Readonly`、`Motion` 顺序执行，
运动受阻后用 `Finish` 进行结束只读检查；重复场次先用 `Init` 创建新证据目录，禁止覆盖旧日志。
环境参数和每条实际命令保存在各组 `*-env.txt` 与 `*-status.json`。
原始证据：`acceptance/l30-slcan-fd-20261016-174520`，
包括控制台、JUnit、退出码、配置、双仓 SHA/状态/差异、二进制哈希、驱动与系统信息、`results.json` 和 `summary.json`。

## Windows SLCAN FD 复测（2026-10-19，UTC+08:00）

本场补测 Windows SLCAN FD 的发现连接和单关节角度收发，保留 2026-10-16 的初始姿态越界与 CLI 受阻记录。
14:24:51–14:25:27，CLI 枚举、发现、发现连接、显式连接及结束再次连接均退出码 0；
**6/6 个实机用例、791 条断言通过，0 失败、0 跳过**。本次初始姿态合法，未开启修正或放宽阈值。

### 环境与设备

- Windows 11 家庭版中文版 x64，`10.0.26200`；MSVC v143 14.44、Windows SDK `10.0.26100.0`、CMake `3.31.8`、Catch2 `3.8.1`，Debug `/MDd`。
- hand：`a6d5a73c4faf844d7ff2a7aa4d996c28dcc6d1eb`，含未提交的文档及 Linux L6 示例整理；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`，工作区干净。本场未修改 SDK 或硬件测试源码，复用本日已验证构建；测试程序、只读入口及两个 SDK 静态库的 SHA256 与 L20 Lite 场次一致。
- 适配器：COM11，USB VID:PID=`16d0:117e`，USB 序列字符串 `3472396E3335`；Microsoft `usbser.inf`，驱动 `10.0.26100.9278`。与 2026-10-16 的 L30 场次及本日四种 Classic 手型使用同一根适配器，商业型号与固件版本未报告。
- 配置：`slcan / canable-fd`，**扩展 CAN FD**，仲裁 1 Mbit/s、数据 5 Mbit/s；UART 115200，串口超时 500 ms，启动等待 100 ms。不设置厂商 CAN DLL，未刷写适配器固件。
- 本场身份实读：L30 右手，NodeID=`1`、HostID=`0`，序列号 `3`，产品码 `LHT30-06-003-R-B-3-G`，固件、硬件及机械版本均为 `V6.1.6`，17 关节。型号及侧别先通过发现确认，再进行显式连接。
- 供电、接线及终端电阻规格未独立测量；CLI 使用场次隔离配置，Catch2 入口使用构建目录 `tests/config`，未加载用户映射。运行前后映射配置均有副本。

### 运行与结果

| 项目 | 结果 | 原始证据 |
| --- | --- | --- |
| COM 枚举 / 发现 | COM11 可枚举，发现一个 L30 右手 Verified 候选，`model=5 side=2 status=0` | `list.log`、`discover.log` |
| 发现连接 / 显式连接 | 身份一致，均读取 17 个角度并正常关闭 | `read.log`、`manual.log` |
| `HandDevice hardware identity refresh and cache` | 1 用例、69 断言通过；身份刷新、缓存及重开一致 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、155 断言通过；17 关节反馈有效、非 stale，连续采样递增 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；关闭、重开及后续反馈正常 | `lifecycle.log/.xml` |
| `HandDevice hardware faults and snapshot projection` | 1 用例、137 断言通过；故障读取、解析及快照投影一致 | `faults.log/.xml` |
| `HandDevice hardware raw angle commands` | 1 用例、235 断言通过；关节 0 `thumb_root`，基准 raw=443、目标 raw=488；使能、目标反馈、姿态恢复与失能成功，`joints=1/17` | `angle-smoke.log/.xml` |
| 运动后显式连接 / 再次角度读取 | CLI 退出码 0；角度用例 1 个、155 断言通过 | `manual-after.log`、`angles-after.log/.xml` |

运动仅选关节 0，步幅 5%、容差 2%、5000 ms 到位上限，未启用初始姿态修正。
命令携带 17 关节向量，其他关节保持采样基准；使能/失能检查成功 ACK 终态，角度命令检查发送完成终态。
实际到位及整手基准恢复由角度查询验证，不以 ACK 或串口写入完成代替机械到位。
结束 CLI 实读角度为 `[444, 598, 188, 129, -8, 716, 806, 808, 720, 810, 695, -2, 1, 5, 803, 699, -515]`，
关节 0 相对采样基准偏差 1 raw，其余关节也在测试恢复容差内，结束已失能。
负数反馈出现在允许负值的侧摆/腕关节，本场未出现此前屈曲关节的初始越界。
未发送整手 50%/100% 目标，未写速度、限流或持久化参数；没有新增人工运动观察记录。
不据此声明所有关节或长期性能已重新验收。

### 复现与证据

只读入口（工作区根目录，按实际构建路径及端口调整）：

```powershell
$Read='.\build-windows-l6-clean\examples\Debug\windows_readonly.exe'
$Cli=@('--backend','slcan','--channel','COM11','--mode','fd','--slcan-protocol','canable-fd',
  '--bitrate','1000000','--data-bitrate','5000000','--serial-baud','115200',
  '--serial-timeout-ms','500','--serial-startup-ms','100',
  '--model','l30','--side','right','--address','1')
& $Read --list @Cli
& $Read --discover @Cli
& $Read --read @Cli
& $Read --manual @Cli
```

硬件用例设置 `HAND_DEVICE_MODEL=l30`、`HAND_DEVICE_SIDE=right`、`HAND_DEVICE_ADDRESS=1`、
`HAND_DEVICE_HOST_ADDRESS=0`、`HAND_DEVICE_BACKEND=slcan`、`HAND_DEVICE_CHANNEL=COM11`、
`HAND_DEVICE_SLCAN_PROTOCOL=canable-fd`、`HAND_DEVICE_BITRATE=1000000`、`HAND_DEVICE_DATA_BITRATE=5000000`、
`HAND_DEVICE_SERIAL_BAUD=115200`、`HAND_DEVICE_SERIAL_TIMEOUT_MS=500`、`HAND_DEVICE_SERIAL_STARTUP_MS=100`。
先完成上述只读用例，再开启 `HAND_DEVICE_ENABLE_MOTION_TESTS=1`，
以关节 0、5% 步幅、2% 容差、5000 ms 上限运行 `HandDevice hardware raw angle commands`，结束后关闭运动开关。

原始证据及 `run-l30-slcan-fd.ps1`（仓库外）：
`acceptance/l30-slcan-fd-20261019-142345`，含双仓 SHA/状态/差异、
二进制哈希、串口/驱动/系统、CLI 与用例日志、JUnit、环境变量/退出码、映射配置和 `results.json`。
该场补齐 L30 的 SLCAN FD 发现与单关节运动恢复检查；USB 拔插、PCAN FD、MiniCANFD、Release、
其他固件及长期负载未在本场测试。此前失败不改记为通过，也不归因于未验证的硬件或固件修复。
