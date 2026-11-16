# 平台与硬件验证范围

[English](../../en/reference/compatibility.md) · [文档索引](../README.md)

## 0.3.12 兼容性与调度验证

现有公开 C++ API 和 Native 默认行为保持可用。触觉阶段调度须显式设置
`RuntimeBehavior::kPython`；支持的协议及调度规则见
[Python 行为基准](../guides/python-behavior-baseline.md#触觉阶段角度调度0312)。
hand 0.3.12 要求 transport 0.3.x 且不低于 0.3.4；消费者须使用匹配的头文件和库重新编译。

L6 位置 Kp/Kd 和扭矩 Kd 已同时提供统一接口及原生 Manager 入口。
O30 统一扭矩上限和运动时间方法须显式选择 `O30Layout::kPythonV25`，原生 Manager 保留挂载 V36 布局。
具体入口见[API 指南](../guides/usage.md#已通过-handdevice-统一的能力)。早期 0.3.0 的接口删除说明不代表当前接口清单。

O6 左手、L20 右手和 O30 右手（显式 V25）已在 Ubuntu 24.04 x86_64 / SocketCAN / Release 下
通过独立查询、仅命令反馈、自适应、五分钟饱和负载及 Native 对照五组调度验证。
固件、适配器、实测源码版本和逐设备结果见[0.3.12 发行说明](../../../RELEASE_NOTES.md#0312)。
本次未新增 L6/L20Lite 调度实机验收，也未实机执行共享 Session、关闭恢复及异常回复时序测试；
生命周期和隔离规则另有离线回归覆盖。这些调度测试不证明频率上限或动态运动精度。

## 历史常用功能实机基线

历史实机验收仅对应各报告记录的 SDK 版本和配置，不自动覆盖后续版本的改动。

`0.3.0` 验证基线为 Ubuntu 24.04 x86_64 / GCC 13 / C++17，以及 Windows 11 x64 /
Visual Studio 2022 / MSVC v143 14.44 / C++17。其他系统、架构、工具链及驱动版本不在
本轮验证范围。Windows Release 使用 `/MD`，Debug 使用 `/MDd`；使用预编译包时匹配
工具链和配置，更新 SDK 后配套重编译应用。

下表的 L6 实机结果均指默认 Classic CAN 协议。可选 HOP CAN FD 协议仅通过离线协议向量和
FakeTransport 验证，尚未完成实机验收。

| 通信路径 | 本轮实机验证范围 |
| --- | --- |
| Linux SocketCAN | L6、O6、L20 Lite、L20、L30、O30 的 SDK 常用功能通过 |
| Windows PCAN Classic | L6、O6、L20 Lite、L20，Debug 适配检查通过 |
| Windows PCAN FD | 后端与离线测试已实现，L30、O30 实机未测 |
| Windows MiniCANFD | O30、L30 实机适配检查通过；O30 使用 Debug 和 x64 `HCanbus.dll 3.3.103.0`，L30 的构建配置及 DLL 版本未记录 |
| Windows SLCAN | 六型号的 Debug 适配检查通过；仅覆盖报告记录的 CANable 配置，仍属实验性接入 |
| Linux 原生 PCAN、MiniCANFD、SLCAN | 本轮没有相应实机通过结论；串口 PTY、动态库测试桩不等于实机 |

Windows 适配检查覆盖发现、打开、身份/角度读取、关闭及一次单关节命令和恢复。
Release 工程、安装和 ZIP 解压接入已于 2026-10-19 通过，和 Debug 真机结果分别记录。
上述结果不覆盖 Release 实机、Windows USB 拔插、长期负载或全关节 Windows 运动；
不保证设备机械定位精度。恢复出厂、EEPROM 持久化和高级调参不属于常用功能
实机验收范围。SLCAN 的 Lawicel 固件不由 CANable 配置的结果覆盖，厂商驱动和 DLL 需
另行获取。详细设备、固件及限制见源码仓库 `docs/hardware-acceptance/README.md`。
