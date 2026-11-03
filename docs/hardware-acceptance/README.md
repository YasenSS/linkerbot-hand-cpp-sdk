# 实机验收

[English summary](README.en.md) | [中文原始记录](README.md)

用于记录开源发布前的硬件验证。每个型号只维护一份 Markdown 报告，包含环境、
源码版本、运行命令、结果和剩余项；后续功能验收更新同一份报告。
原始日志、JUnit、源码副本等调试证据保存在仓库外，不随开源文档提交。
下文的 `acceptance/<批次>/` 是仓库外归档批次标识，已省略执行机器的个人目录，
不是仓库内可点击下载的日志目录。

下表汇总各型号已有 Linux 与 Windows 场次。实测及离线结果、源码版本和未测范围
统一记录在对应型号报告，不能从一个平台或适配器的结果推导其他组合通过。
Windows 的操作步骤见 [Windows 验收](windows.md)。
公开支持基线与未测路径见 [支持与兼容性策略](../zh/reference/compatibility.md#平台与硬件验证范围)。
已追加 Windows / PCAN Classic / Debug 记录的型号为 [L6](l6/report.md)、[O6](o6/report.md)、[L20 Lite](l20lite/report.md) 和 [L20](l20/report.md)；
四种手型的发现、读取及单关节运动恢复均通过，L20 此前 CLI 受阻的记录保留。
[L6](l6/report.md)、[O6](o6/report.md)、[L20 Lite](l20lite/report.md) 和 [L20](l20/report.md) 已完成 Windows / SLCAN Classic / Debug 的发现、读取及单关节收发恢复；
L20 的首次详细日志模式快照过期及正常输出复测记录保留在报告中。
[L30](l30/report.md) 已完成 Windows / SLCAN FD / Debug 的发现、读取及单关节运动恢复，历史姿态越界及 CLI 受阻记录保留。
[O30](o30/report.md) 已追加 Windows / MiniCANFD 和 SLCAN FD / Debug 记录，两条路径的发现/显式连接及单关节收发恢复通过；
MiniCANFD 需使用该场次验证的配套 DLL，旧版 DLL 的发送失败记录保留在报告中。
具体通过项与受阻项以报告为准。

| 型号 | 报告 / 状态 |
| --- | --- |
| L6 | [Linux 常用功能 34 项通过；Windows PCAN / SLCAN Classic 读取及单关节收发恢复通过](l6/report.md) |
| O6 | [Linux 常用接口 33 项最终通过；Windows PCAN / SLCAN Classic 发现、读取及单关节收发恢复通过](o6/report.md) |
| L20Lite | [Linux 角度到位/联动差异保留；Windows PCAN / SLCAN Classic 发现、读取及关节 0 运动恢复通过](l20lite/report.md) |
| L20 | [常用接口 32 项通过；Windows PCAN / SLCAN Classic 发现、读取及单关节收发恢复通过，历史设备故障及复测记录见报告](l20/report.md) |
| L30 | [Linux 34 项通过、另有跳过项；Windows SLCAN FD 发现、读取及单关节运动恢复通过](l30/report.md) |
| O30 | [Linux SDK 常用接口验证通过；Windows MiniCANFD / SLCAN FD 连接及单关节收发恢复通过](o30/report.md) |

## Windows 实测矩阵

更新日期：2026-10-19。下表仅汇总 **Windows Debug** 的真实设备适配检查，
“通过”包含枚举、发现、发现/显式连接读取、关闭释放及一次单关节小幅运动与恢复；
“部分通过”列明尚缺项目，“未测”表示没有对应组合的实机记录，
“—”表示 [本轮指南](windows.md) 未列该路径，不代表适配器硬件不支持。

| 手型 / 详细报告 | PCAN | MiniCANFD | SLCAN（CANable 配置） |
| --- | --- | --- | --- |
| [L6](l6/report.md) | Classic 通过 | — | Classic 通过 |
| [O6](o6/report.md) | Classic 通过 | — | Classic 通过 |
| [L20 Lite](l20lite/report.md) | Classic 通过 | — | Classic 通过 |
| [L20](l20/report.md) | Classic 通过 | — | Classic 通过 |
| [L30](l30/report.md) | FD 未测 | 未测 | FD 通过 |
| [O30](o30/report.md) | FD 未测 | 通过 | FD 通过 |

SLCAN 结果均对应明确选择的 CANable FD 固件配置，不覆盖 Lawicel Classic 固件。
L6/O6/L20 Lite/L20 的 Classic CAN 及 L30 的扩展 FD 使用同一根 COM11 适配器
（USB 序列字符串 `3472396E3335`）；O30 标准 FD 使用另一根 COM10 适配器
（`3477397D3335`）。MiniCANFD 的 O30 结果使用已验证的 `HCanbus.dll 3.3.103.0`。

本矩阵不代表全关节、Release 实机、USB 拔插恢复或长期性能验收通过。
L20 Lite 历史关节 7/8 到位及联动问题、L20 首次快照过期、L30 历史姿态越界及 CLI 受阻记录均保留；
复测结果和未测范围以各型号报告的对应日期/适配器场次为准。

2026-10-19 已补齐 Windows Release 工程及发行包验收：
核心 CTest 143/143、安装与解压消费 4/4、219 个公开头文件、10 个文档示例及 9 组版本兼容检查通过。
该结果单独记录，不将上述 Debug 实机矩阵直接扩展为 Release 实机通过。

## 运行角度测试

在 SDK 根目录先构建并确认全部 6 个用例存在；列出用例不会连接设备：

```bash
cmake --build build --target tests --parallel 4
./build/tests/tests --list-tests "[hardware][device][angles]"
```

已连接且允许运动的 L30 使用以下配置，地址按实际设备调整：

```bash
env -u HAND_DEVICE_ANGLE_JOINT \
  HAND_DEVICE_MODEL=l30 CAN_INTERFACE=can0 \
  HAND_DEVICE_ADDRESS=1 HAND_DEVICE_HOST_ADDRESS=0 \
  HAND_DEVICE_ENABLE_MOTION_TESTS=1 HAND_DEVICE_ANGLE_NORMALIZE_START=1 \
  HAND_DEVICE_ANGLE_NORMALIZE_MAX_PERCENT=25 \
  ./build/tests/tests "[hardware][device][angles]" --order decl
```

运行参数与用例说明见 [硬件测试说明](../../tests/hand/device/hardware/README.zh-CN.md)。
本次 L30 的初始修正上限为 25%，代码默认值为 20%。

## 报告与发布要求

按下方 [报告模板](#报告模板) 填写型号目录下的 `report.md`。
只有实际执行、无失败且无跳过的用例才记为通过；`No tests ran` 不算通过。
需要保留机器报告时，将 Catch2 `--reporter` 和 `tee` 输出到仓库外目录。

发布真机验收涵盖角度、ACK／无 ACK 终态、连续目标替换、断连恢复、缓存失效和
旧命令不重放等项目，并记录固件、平台、适配器/驱动与源码版本。L30 本轮结果见
报告。固定最终源码后按代码变化确认受影响的验证范围；文档更新不要求重复全部实机测试。
未完成验收的通信组合须明确支持边界。

## 设备发现检查

按发布范围选择实际平台、适配器、固件和帧格式组合，结果直接写入对应型号报告。
先核对供电、接线、终端电阻和速率，确认手处于安全区域；Linux SocketCAN 由部署方
预先配置，MiniCANFD 使用明确库路径与设备/通道，SLCAN 只访问明确允许的端口。
发现阶段仅查询身份，不使能或发送运动目标。

每个实际验证的组合检查：

1. 枚举与发现返回预期通道、型号、侧别和地址，保留 Partial candidate 及 failure 诊断。
2. `create_hand_device()` 不建立 Transport 或产生总线 I/O；`open()` 重新读取并核对物理身份。
3. 关闭扫描与连接后资源释放，可重新打开；记录观察方法、耗时及日志/抓包。

Classic、扩展 FD 和标准 FD 分别记录；SLCAN 记录 Lawicel Classic 或 CANable FD 固件协议，
不能以离线编解码通过代替真实串口验收。需要验证多适配器部署时，另查一个适配器
占用/断开不影响其他候选，failure 能定位故障 backend/通道；需要跨语言一致性时，
用同一设备与适配器对照 Python SDK 的型号、侧别和地址。未做的检查明确记录未执行。

## 报告模板

每种型号一份报告，按日期/平台追加场次。只写本场范围，通用功能验收与 Windows
适配检查分别记录；不预填其他平台的待执行清单。

```text
场次日期 / 执行人员 / 平台 / 验收范围：
hand / comm 完整 SHA、工作区修改及差异证据：
OS / 架构 / 编译器 / SDK / 构建配置 / 运行库：
设备型号 / 侧别 / 地址 / 序列号 / 产品码 / 固件：
适配器型号 / 序列号 / 固件 / 驱动 / DLL 版本及哈希：
backend / 通道 / 帧格式 / CAN 与 UART 速率 / FD 时序：
供电 / 接线 / 终端电阻：
命令或筛选 / 用例及断言数 / 失败 / 跳过 / 退出码：
结果及未执行项（发现、身份、读取、控制、恢复等按本场范围填写）：
人工观察 / 残留问题 / 支持边界 / 冻结版本复测要求：
原始证据位置：
```

没有来源的字段写“未报告”，不可观测项写“不可观测”。离线检查、实机收发与长期负载
分别给出结论；后续改源码、适配器、驱动或固件时记录新的验证范围。
