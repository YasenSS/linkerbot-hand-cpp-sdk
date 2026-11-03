# C++ Report Card 报告快照

[English](README.md) | [简体中文](README.zh-CN.md)

[![C++ Report Card](badge.svg)](https://github.com/soulteary/linkerbot-hand-cpp-sdk/actions/runs/41736844903)

此 SVG 原样取自成功的 [ARM64 完整分析运行](https://github.com/soulteary/linkerbot-hand-cpp-sdk/actions/runs/41736844903)。
对应 main 提交为 `12cb694a3c5534f41372fd40a67d11bb03e33471`，使用 C++ Report Card Action
v1.0.2，固定提交为 `9458d052f286c576d557c34d8ad43096cedbf561`。

- 分析完整：`true`；等级 **B**；分数 **84.68/100**。
- clang-format：**581/581 文件**，0 条诊断。
- Cppcheck：**342/342 编译单元**，19 条诊断。
- clang-tidy：**342/342 编译单元**，202 条诊断。

评分是源码分析基线，不代表测试覆盖率或实机验证。诊断需要逐项复核，并非全部都是已确认缺陷。
当前门禁要求分析完整且 error 级别诊断为零，并保留警告，因此工作流成功不代表零诊断。

README 评分 badge 展示此固定快照；旁边的 CI badge 展示 main 分支最近一次工作流状态。
对应运行的 `cppreportcard-ubuntu-24.04-arm` 附件包含原始 JSON、Markdown、SVG 和编译数据库。
后续运行会上传附件，不会自动提交。更新快照时，应从已完成的运行取得 SVG，
同步更新来源提交、运行链接和统计。[质量指南](../../docs/zh/guides/quality.md)说明范围与门禁策略。

本轮从 F（59/100、717 条诊断）改善至 B（84.68/100、221 条诊断），error 级别诊断为零。
修复与剩余事项见[诊断复核](../../docs/zh/guides/quality-review.md)。
