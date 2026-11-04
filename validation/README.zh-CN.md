# 验证工具

[English](README.md) | [简体中文](README.zh-CN.md)

- [构建与验证](../docs/zh/guides/validation.md)：离线目标、安装消费者与 CI 排错。
- [质量分析](../docs/zh/guides/quality.md)：Report Card 范围与完整性政策。
- [异步行为向量](README-async.zh-CN.md)：可选的 C++/Python 对照。
- [硬件证据](../docs/hardware-acceptance/README.md)：历史报告与边界。

执行 `python3 validation/check_docs.py` 检查文档导航和基线。
执行 `python3 -m unittest discover -s validation -p 'test_quality_tools.py'` 验证分析辅助脚本。
`test_release_*.py` 覆盖发布检查，使用临时仓库与包。
`check_headers.py`、`check_documented_examples.py` 需要已安装的 SDK prefix。
