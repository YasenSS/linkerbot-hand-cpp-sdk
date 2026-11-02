# C++ SDK 中文文档

[English](../en/README.md) · [文档总览](../README.md) · [项目首页](../../README.md)

## 推荐阅读顺序

1. **构建与安装**：Linux 从[项目快速开始](../../README.md#快速开始linux)入手；Windows 阅读[构建与应用接入](guides/windows.md)。
2. **建立连接**：先[准备通信接口](connections/prepare-interface.md)，再根据部署选择[自动连接](connections/automatic.md)或[手动连接](connections/manual.md)。Windows 适配器的命令行步骤见[适配器接入](connections/windows-adapters.md)。
3. **使用型号功能**：按实际设备选择下方教程，示例说明读取、控制、命令结果与传感器数据。
4. **扩展应用**：阅读[API 使用指南](guides/usage.md)，了解统一 HandDevice、专有 Manager、异步命令、共享总线、轮询及关闭恢复。
5. **核对支持边界**：查阅[型号能力表](reference/model-capabilities.md)及[平台与硬件验证范围](reference/compatibility.md)；发现问题时阅读[设备发现参考](reference/discovery.md)。

## 型号教程

| 型号 | 教程与支持状态 |
| --- | --- |
| L6 | [Classic CAN 功能与专有 Manager](tutorials/l6.md)；可选 HOP CAN FD 见[协议说明](guides/usage.md#l6-可选-can-fd-协议) |
| O6 | [角度、速度、加速度与传感器](tutorials/o6.md) |
| L20 Lite | [十关节控制、配置缓存与触觉](tutorials/l20lite.md) |
| L20 | [十六关节控制与传感器](tutorials/l20.md) |
| L30 | [CAN FD、使能与十七关节控制](tutorials/l30.md) |
| O30 | [CAN FD、二十关节控制与动态传感器](tutorials/o30.md) |
| O20 | [当前不支持的说明](tutorials/o20.md) |

## 指南与参考

| 文档 | 用途 |
| --- | --- |
| [Windows 使用指南](guides/windows.md) | 环境、构建、安装和应用接入 |
| [API 使用指南](guides/usage.md) | 统一接口和型号 Manager 的操作方式及生命周期 |
| [Windows 适配器接入](connections/windows-adapters.md) | PCAN、MiniCANFD、SLCAN 参数、命令与排错 |
| [各型号能力说明](reference/model-capabilities.md) | 功能矩阵、读写来源、数值范围和固件限制 |
| [设备发现](reference/discovery.md) | 扫描配置、完整/部分身份、候选及失败诊断 |
| [平台与硬件验证范围](reference/compatibility.md) | 已验证环境、通信路径与未测边界 |

## 实机记录与贡献

- [实机验收和型号报告](../hardware-acceptance/README.md)：保留各场次版本、设备、结果及遗留问题。
- [Windows 真机验收](../hardware-acceptance/windows.md)：适配器发现、读取及单关节运动恢复检查。
- [贡献说明](../../CONTRIBUTING.md)：问题反馈、代码约定和验证要求。

教程负责具体步骤，指南解释通用用法，参考说明集中维护能力与限制；历史实测报告由两种语言共用。
新增或移动文档时，同时更新[英文对应页](../en/README.md)、导航、交叉链接、安装与发布包检查中的路径。
