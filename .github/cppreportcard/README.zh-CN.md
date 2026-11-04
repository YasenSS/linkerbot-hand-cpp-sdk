# C++ Report Card 报告快照

[English](README.md) | [简体中文](README.zh-CN.md)

[![C++ Report Card](badge.svg)](https://github.com/linker-bot/linkerbot-hand-cpp-sdk/actions/runs/41852646930)

此 SVG 原样取自已完成的 [ARM64 完整分析](https://github.com/linker-bot/linkerbot-hand-cpp-sdk/actions/runs/41852646930)，
对应 main 提交 `c858db4f7046d11b466bdff81068b9199c19045c`，使用 C++ Report Card Action
v1.0.2，固定提交 `9458d052f286c576d557c34d8ad43096cedbf561`。
当前工作流已固定 v1.0.4；此快照保留实际运行时的版本信息。

- 分析完整：`true`；等级 **A+**；分数 **100/100**；零诊断。
- clang-format：**588/588 文件**。
- Cppcheck：**345/345 编译单元**。
- clang-tidy：**345/345 编译单元**。

CI 要求三个工具均完整分析、A+、100 分且零诊断。
评分衡量源码分析结果，不代表测试覆盖或实机验证。
README 评分徽章展示此固定快照，旁边的 CI 徽章展示 main 分支最近一次工作流状态。
运行附件 `cppreportcard-ubuntu-24.04-arm` 包含原始 JSON、Markdown、SVG 和编译数据库。
报告不会自动提交。刷新快照时须从已完成的运行同步更新 SVG、来源提交、运行链接和统计。
范围与门禁策略见[质量指南](../../docs/zh/guides/quality.md)。

历史 F/B 报告及[诊断复核](../../docs/zh/guides/quality-review.md)保留为基线与修复过程的证据。
