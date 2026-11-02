# C++ SDK 文档

[简体中文](README.md) | [English](README.en.md)

- [中文文档](zh/README.md)：接口准备、设备连接、型号教程、通用指南与参考说明。
- [实机验收与报告](hardware-acceptance/README.md)：共用的历史验收记录。

## 文档收纳约定

`zh` 与 `en` 使用相同的目录层级和文件名，仅语言目录不同。

| 目录 | 内容 |
| --- | --- |
| `connections/` | 接口准备、自动/手动连接、适配器操作与排错 |
| `tutorials/` | 各型号的分步功能示例与支持状态 |
| `guides/` | 构建安装、通用 API、异步命令、轮询与生命周期 |
| `reference/` | 能力表、数值与固件限制、发现机制、兼容性 |
| `hardware-acceptance/` | 按型号集中保存的历史实机报告，两种语言共用 |

新增教程按型号放入 `tutorials`，通用操作放入 `guides`，连接步骤放入 `connections`。
数值范围和固件边界集中维护在 `reference`，其他页面通过链接引用；验收结论以对应日期、版本和硬件报告为准。
更新文档时同步维护中英文对应页面、语言导航及受影响的链接。
