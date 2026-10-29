# O30 实机验收报告

本文为删除 O30 运动时间、扭矩上限功能之前的历史验收记录；其中接口及配置描述仅对应记录的 SDK 版本。

当前结论：**O30 已通过 SDK 常用接口实机验证**。按用户确认的验收范围，以命令下发、回执、设备目标读回、反馈接口、配置恢复和断电恢复为判据；机械运动到位精度不纳入此次 SDK 通过判据。下表保留实际运动测试结果，不将到位失败或未执行的断言改记为通过。

Windows / MiniCANFD / Debug 于 2026-10-19 完成枚举、发现/显式连接、20 关节读取、关闭重连和一次单关节角度收发及恢复；5 个标准硬件用例、816 条断言通过，无失败或跳过。实测使用 x64 HCanbus.dll `3.3.103.0`；旧版 DLL 的发送失败记录保留在文末。本结果不覆盖全部关节、其他 DLL、Release 或 USB 拔插。

同日 Windows / SLCAN FD / Debug（COM10，CANable FD 协议）也完成发现/显式连接、20 关节读取、关闭重连、单关节控制及恢复，另有 5 个用例、816 条断言通过，无失败或跳过。具体适配器身份和复用边界见最后一节。

## 环境

- 日期：2026-10-14，UTC+08:00；更新同一份报告，覆盖 2026-10-13 的未完成项目。
- O30 右手，HOP 请求端点 `0x001`；协议 `0.0.4`，固件 `1.1.0`，硬件 `V0.9.0`，机械版本 `3.0.0`。协议未提供序列号或设备地址。
- Ubuntu 24.04.3 x86_64，Linux `7.0.0-38-generic`，GCC 13；SocketCAN `can0`，CAN FD，仲裁 1 Mbit/s、数据 5 Mbit/s。
- 适配器：`candleLightFD gs_usb`，VID:PID `1d50:606f`，驱动 `gs_usb`。
- hand 测试基线：`1aa3d7c38938a776a7f5ef6c7ed880a388a08b24` 加本轮 O30 测试修复；对应工单 #198，测试提交 `d73486ad82574f2e869d131d0fad417a465776e7`。comm：`18f7e0e87f4710fc68af7b66d6cb5cdf97018b96`。
- 运动与配置写入通过公开 `HandDevice`；O30 原角度目标的只读查询使用已有型号接口辅助，实际运动结果用公开角度反馈判定。

## 测试结果

| 项目 | 本轮结果 |
| --- | --- |
| 身份、生命周期、读取、输入校验、轮询、快照、事件流和控制能力拒绝 | 20/20 用例通过，6814 条断言。 |
| ACK、缺 ACK 超时、参数复制、raw/percent 目标替换、发送失败、取消与旧目标不重放 | 7/7 用例通过，734 条断言；包含实际角度发送，使用原有目标保持姿态。 |
| 速度、限流逐通道变更与恢复 | 两个 section 均通过，3690 条断言，各覆盖 20 通道，主动查询确认恢复原配置。 |
| 逐关节 raw / percent 运动 | 全部 20 个关节的两种方式均已执行，不因某个失败而漏掉后续关节。首次批次各 17/20 通过；修复恢复基准后，关节 4 的 percent 单独复测通过。 |
| 昨天失败的关节 6、7 | 本轮 raw / percent 均到位并恢复；原速度为 0，运动时临时设为 64，并读回确认，结束恢复为 0。 |
| 关节 4（无名指侧摆） | 恢复检查的原有反馈偏差误判已修正；percent 复测通过。raw 单独复测目标 149、反馈 166，失败；随后综合用例中的该关节通过，结果不一致，保留失败记录。 |
| 关节 8（中指第一根部） | raw 目标 34、实际 21；percent 对应目标 33、实际 20。设备目标读回与 SDK 发出的完整数组一致，但实际到位偏差约 13 raw，仍失败。 |
| 关节 19（小指指尖） | raw / percent 对应目标均为 26，设备目标读回正确，实际保持 0；仍失败。 |
| 综合角度用例 | 未打开状态校验、读取、非法输入和 raw 关节 0–7 检查通过；在关节 8 失败，1741/1742 条断言通过。后续 raw 关节和综合 percent 阶段未执行，覆盖由单关节用例另行记录。 |
| 真实断电恢复 | 最终标准用例通过，34 条断言：断电后读取失败；上电并恢复系统 CAN 接口后，同一对象 `recover()` 成功、身份字段一致、代次增加、轮询和新快照恢复。 |
| 离线回归 | CTest 4/4 通过。 |

角度步幅为 10%，容差为 4%（约 10.2 raw），等待 5 秒，要求连续两次反馈达标。步幅大于两倍容差，不把无运动记为通过。按用户要求不再逐次要求现场观察。
运动前保存原速度配置，仅将关节 6–10 的速度从 0 临时改为 64；未修改电流限制、力矩上限或 EEPROM。

## 运行方式

在 SDK 根目录构建 `tests`，设置共同环境。运动前通过 `get_configured_speeds()` 保存原配置，使用 `set_speeds()` 建立上述临时速度并确认读回，退出时恢复原数组。

```bash
export HAND_DEVICE_MODEL=o30 HAND_DEVICE_SIDE=right HAND_DEVICE_ADDRESS=0x001 CAN_INTERFACE=can0
export HAND_DEVICE_ENABLE_MOTION_TESTS=1 HAND_DEVICE_ENABLE_FAULT_TESTS=1
export HAND_DEVICE_ANGLE_STEP_PERCENT=10 HAND_DEVICE_ANGLE_TOLERANCE_PERCENT=4 HAND_DEVICE_ANGLE_SETTLE_MS=5000
./build/tests/tests '[hardware][device][commands]' --order decl
# 将关节变量依次设置为 0–19，分别运行两个用例；这里展示尚未到位的关节 8。
HAND_DEVICE_ANGLE_JOINT=8 ./build/tests/tests 'HandDevice hardware raw angle commands,HandDevice hardware percent angle commands' --order decl
env -u HAND_DEVICE_ANGLE_JOINT ./build/tests/tests 'HandDevice hardware full angle acceptance'
```

真实断连按 [硬件测试说明](../../../tests/hand/device/hardware/README.md) 操作，本轮每步等待 15 分钟。保持 USB 适配器连接，只切断手部供电。第一次上电后虽然接口仍显示 CAN FD，但独立身份查询也无应答；系统接口重置后通信恢复。第二次在上电后先执行以下命令，再通知标准用例进行 SDK 恢复，完整通过：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can0 up
```

本结果需要系统接口重置配合，不宣称适配器自动恢复或 USB 热插拔问题已经解决。

## 修复与结束状态

- 统一 O30 角度基准辅助代码；角度和静止命令契约均保留设备原有目标，避免将反馈偏差累积写回。
- 静止契约检查原目标未变及实际姿态稳定；运动仍验证接近新目标，恢复则验证原目标读回一致、反馈回到测试前位置，不用原有偏差误判恢复失败。
- 新增 O30 写入后的完整目标读回断言，关节 8、19 的最终复测已确认编码、关节位置和目标读回正确；实际到位失败的原因尚未确定。
- 结束时 20 个关节的角度目标、速度配置、电流限制和力矩上限均与开始时一致，均已读回核对；故障码全 0，温度原始值最高 69。原始单位未确认，不据此推断过热原因。
- 恢复出厂、参数持久化、实际高级调参及力矩输出未执行。原始日志和临时准备程序保存在仓库外。
- SDK 常用接口验收已完成；20/20 运动到位和综合角度用例未全部通过，列为设备运动验证记录，不作为本次 SDK 开源验收的阻塞项。提交并冻结本轮修复后复验 SDK 项目。

## Windows MiniCANFD 适配实测（2026-10-19，UTC+08:00）

本场仅验证 Windows MiniCANFD 接入、读取和一次单关节小幅控制，不重复 Linux 通用功能验收。
标准硬件用例于 10:01–10:02 执行，**5/5 用例、816 条断言通过，0 失败、0 跳过**。
枚举、发现连接和显式连接的 CLI 检查另计，最终均退出码 0。

### 环境与设备

- Windows 11 家庭版中文版 x64，25H2，`10.0.26200.9445`；MSVC v143 14.44.35207 / `cl 19.44.35229.0`，Windows SDK `10.0.26100.0`，CMake `3.31.8`，Catch2 `3.8.1`；Debug `/MDd`。
- hand：`62767b5153dd1097813b99dc69ef633d660caeee`；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`。开始测试时两仓工作区干净；本次报告为测试后追加。
- 复用已验证的 Debug 构建；核对 hand 的 SDK 源码与配置目录修复后的构建来源无差异，comm 源码未变。本场未重跑全量离线测试或发行包验收，二进制哈希另存证据。
- MiniCANFD：厂商枚举为 FD 原生通道 `0:0`；USB VID:PID=`a8fa:8598`，实例标识尾段 `F0802069319C5431`。设备管理器显示 WinUSB；驱动 `10.0.26100.9444`，`winusb.inf`。厂商适配器序列号/固件未报告。
- 最终 DLL：`HCanbus.dll`（x64；省略本机存放目录），x64，文件/产品版本 `3.3.103.0`，118272 字节，SHA256 `1c7a7fdb2b78f276cea84127f9ad72af6f7fe4004132f151586cd7899db8c5a8`。
- 连接参数：仲裁 1 Mbit/s、数据 5 Mbit/s，标准 CAN FD、无 BRS；HOP 请求端点 `0x001`。同一参数下完成全部成功场次，没有改动手的速率配置。
- 本场身份实读：O30，发现结果为右手，产品码 `O30`，固件 `1.1.0`、硬件 `V0.9.0`、结构 `3.0.0`；序列号未报告。请求端点是连接参数，不当作设备报告地址。本场未单独输出协议版本或设备 UID，不沿用旧场次的这些字段作为新证据。
- 接线、终端电阻和供电规格未独立测量；测试进程使用证据目录内的隔离配置。

### 运行与结果

| 项目 | 结果 | 原始证据 |
| --- | --- | --- |
| MiniCANFD 枚举 | `0:0 FD=yes`，退出码 0 | `list-compatible.log` |
| 身份发现 | 一个 O30 右手候选，`model=6 side=2 status=0`，退出码 0 | `discover-compatible.log` |
| 发现连接 / 显式连接 | 两种入口均读到 O30 固件 1.1.0 和 20 个角度，正常关闭，退出码 0 | `read-compatible.log`、`manual-compatible.log` |
| `HandDevice hardware identity refresh and cache` | 1 用例、66 断言通过；身份读取、刷新/缓存及关闭重开一致 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、179 断言通过；20 关节有效、非 stale、有限反馈及递增时间戳 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；关闭/重开、资源释放、身份及新反馈正常 | `lifecycle.log/.xml` |
| `HandDevice hardware raw angle commands` | 1 用例、352 断言通过；关节 0 `thumb_rotation`，原目标 raw=100、新目标 raw=113，目标读回、反馈及原目标恢复通过；`joints=1/20` | `angle-smoke.log/.xml` |
| 结束后显式连接 / 角度读取 | CLI 退出码 0；角度用例 1 个、179 断言通过，重新读取 20 关节正常 | `manual-after.log`、`angles-after.log/.xml` |

运动参数为仅关节 0、5% 步幅、2% 容差、5000 ms 上限，未启用初始姿态修正。
运动命令通过公开 `HandDevice`，O30 辅助接口只读查询原目标，且与 HandDevice 共享同一 Session。
完整目标向量保留其他关节原目标；恢复时确认原目标数组一致，反馈回到测试前位置。
关节 0 原目标为 100，开始和结束的实际反馈均为 108，未将这个已有偏差写回成新基准。
未写入速度、限流、持久化参数或力矩配置，也没有人工运动观察记录。

开始和结束的 20 关节 raw 反馈相同：
`[108, 5, 197, 182, 135, 97, 2, 0, 0, 2, 0, 0, 0, 0, 0, 1, 3, 1, 0, 0]`。
本次仅覆盖一个关节的 Windows 收发；不改变历史 Linux 场次中其他关节到位失败的记录。

### DLL 版本差异与复现

首次使用本机旧 DLL `3.1.107.0`（111616 字节，SHA256
`85290ad8bb52ece26c6b3ec81ddde74d8957f1d6afe9ffa43348040681f869a2`）时，
枚举和打开/初始化能完成，但发现与显式连接均报 `CANFD_Transmit failed: expected 1 frame, got 0`，
没有收到手身份，未执行运动。其路径为 `dashboard-test\libcanbus\HCanbus.dll`。
本机配套发布包说明指出应使用新版 DLL；切换到上述 `3.3.103.0` 后，同设备、同源码和同速率全部通过。
这是本场两个 DLL 样本的兼容性结果，不能仅凭文件名或枚举成功推断可用。

只读复现命令（路径、通道、侧别及端点按实际设备填写）：

```powershell
$Read='.\build-windows-l6-clean\examples\Debug\windows_readonly.exe'
$Library='C:\厂商组件\MiniCANFD\x64\HCanbus.dll' # 替换为同版本 DLL 的实际路径
$Cli=@('--backend','minicanfd','--channel','0:0','--mode','fd','--library',$Library,
  '--bitrate','1000000','--data-bitrate','5000000','--model','o30','--side','right','--address','1')
& $Read --list --backend minicanfd --library $Library
& $Read --discover @Cli
& $Read --read @Cli
& $Read --manual @Cli
```

真机用例设置 `HAND_DEVICE_BACKEND=minicanfd`、`HAND_DEVICE_CHANNEL=0:0`、显式 DLL 路径、
型号/侧别/端点及 1M/5M 速率后运行；运动另设关节 0 和前述步幅/容差/等待参数。
执行脚本及原始证据（仓库外）：`acceptance/o30-minicanfd-20261019-095258`，
含旧 DLL 失败日志、两版 DLL 哈希/版本、最终 CLI/用例控制台、JUnit、环境变量、退出码、双仓 SHA/状态、
`results.json`、`summary.json` 和脚本副本。USB 拔插、Release、其他 DLL 和长期性能未执行。

## Windows SLCAN FD 适配实测（2026-10-19，UTC+08:00）

本场更换为 SLCAN FD 适配器，先读取 O30 身份，再执行单关节 5% 小幅运动及恢复。
10:32–10:33 共 **5/5 个实机用例、816 条断言通过，0 失败、0 跳过**；
CLI 枚举、发现、发现连接、显式连接及结束再次连接均退出码 0。

### 环境与身份

- Windows 11 x64，25H2，`10.0.26200.9445`，MSVC v143 14.44，Debug `/MDd`；复用同一套已验证 Debug 构建，未因换适配器重跑全部离线检查。
- hand：`9e25a09b75e39e25a8875a24bcd1fb19e9d2c114`；comm：`e1a16ad904e501915f85d1fb71db0900e5ceb285`。测试前 hand 工作区包含 README、适配器说明及示例的未提交改动；SDK/硬件用例源码未改，comm 工作区干净，状态/差异/二进制哈希已保存。
- 适配器：COM10，VID:PID=`16d0:117e`，USB 序列字符串 `3477397D3335`；Microsoft `usbser.inf`，驱动 `10.0.26100.9278`。商业型号、固件版本未报告，不能仅凭 USB 名称识别固件。
- 显式使用 `slcan / canable-fd`，标准 CAN FD、无 BRS，仲裁 1 Mbit/s、数据 5 Mbit/s；UART 115200，串口超时 500 ms、启动等待 100 ms。不设置厂商 CAN DLL。
- 实读 O30 右手，产品码 `O30`，固件 `1.1.0`、硬件 `V0.9.0`、结构 `3.0.0`，20 关节；序列号未报告。HOP 请求端点 `0x001` 为连接参数，不是设备报告地址。协议版本、设备 UID、适配器固件版本未单独输出，不沿用旧值。
- 接线、终端电阻及供电规格未独立测量；测试进程使用本场隔离配置。实际 O30 身份/反馈和目标读回证明当前协议配置可用，不以串口字节写完代替设备回复。

### 运行与结果

| 项目 | 结果 | 原始证据 |
| --- | --- | --- |
| 串口枚举 / 发现 | 明确允许的 COM10 可枚举；发现一个 O30 右手 Verified 候选，`model=6 side=2 status=0` | `list.log`、`discover.log` |
| 发现连接 / 显式连接 | 两种入口身份一致，均读回 20 个角度并正常关闭 | `read.log`、`manual.log` |
| `HandDevice hardware identity refresh and cache` | 1 用例、66 断言通过 | `identity.log/.xml` |
| `HandDevice hardware angle readback` | 1 用例、179 断言通过；20 关节反馈有效、非 stale 且采样时间递增 | `angles-read.log/.xml` |
| `HandDevice hardware lifecycle and reopen` | 1 用例、40 断言通过；关闭、重开、身份及新反馈正常 | `lifecycle.log/.xml` |
| `HandDevice hardware raw angle commands` | 1 用例、352 断言通过；关节 0 `thumb_rotation`，原目标 raw=100、新目标 raw=113，完整目标读回及实际反馈/恢复检查通过；`joints=1/20` | `angle-smoke.log/.xml` |
| 结束后显式连接 / 角度读取 | CLI 正常退出；角度用例 1 个、179 断言通过 | `manual-after.log`、`angles-after.log/.xml` |

运动仅选择关节 0，步幅 5%、容差 2%、等待上限 5000 ms，未启用初始姿态修正。
沿用 O30 原目标数组作为控制/恢复基准，其他关节原目标保持；开始的关节 0 实际反馈为 108，
结束为 105，恢复检查满足用例容差，原目标数组恢复一致。
未运行整手 50% 示例，未写速度、限流或持久化配置；未新增人工运动观察记录。

### 这根适配器的其他型号复用范围

当前串口已证明兼容所用 CANable FD 协议配置的 O30 标准 FD 路径。该协议的 SDK 实现同时提供
Classic 和扩展 FD 编解码，因此可以作为其他型号的 SLCAN 测试适配器候选：

| 手型 | 测试模式 | 同一固件的配置方式 | 本根适配器实测状态 |
| --- | --- | --- | --- |
| L6、O6、L20 Lite、L20 | Classic CAN | 保留 `canable-fd` 配置，`--mode classic`，不传 FD 数据速率；C++ `SlcanConfig::fd=false` | 未连接这些型号，不能标为实机通过 |
| L30 | 扩展 CAN FD | `--mode fd --slcan-protocol canable-fd`，配置实际 NodeID/HostID 及速率，由手协议生成扩展帧 | 本根未测；此前 L30 场次 USB 序列字符串为 `3472396E3335`，属于另一根适配器 |
| O30 | 标准 CAN FD，无 BRS | 当前 COM10、CANable FD、1M/5M、端点 1 | 本场读取、单关节目标/反馈及恢复通过 |

上述是按 CANable FD 协议实现判断的复用能力；当前固件版本未报告，其他帧格式仍需换手后做
一次简短的身份和角度读取确认。不能将本根结果记成 Lawicel Classic 固件验收，
也不需要因为接 Classic 手就盲目刷写固件或改为等待串口 ACK 的 Lawicel 配置。

只读复现：

```powershell
$Read='.\build-windows-l6-clean\examples\Debug\windows_readonly.exe'
$Cli=@('--backend','slcan','--channel','COM10','--mode','fd','--slcan-protocol','canable-fd',
  '--bitrate','1000000','--data-bitrate','5000000','--serial-baud','115200',
  '--serial-timeout-ms','500','--serial-startup-ms','100','--model','o30','--side','right','--address','1')
& $Read --list @Cli
& $Read --discover @Cli
& $Read --read @Cli
& $Read --manual @Cli
```

原始证据及 `run-o30-slcan.ps1` 副本（仓库外）：
`acceptance/o30-slcan-fd-20261019-103231`，含身份、CLI/用例控制台、JUnit、
环境变量及退出码、双仓版本/状态/差异、串口/驱动、二进制哈希及结果摘要。
本场未做 USB 拔插、Release、长期负载或其他型号验证；各型号 Linux 历史结果保持原结论。
