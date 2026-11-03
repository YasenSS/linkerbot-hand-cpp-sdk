# 开发工具

[English](README.md) | [简体中文](README.zh-CN.md)

## L6 右手设备信息读取

```bash
cmake --build build --target l6_read_version
./build/tools/l6_read_version can0
```

读取序列号、固件/PCB/机械版本，以及一帧角度反馈。默认接口为 `can0`，
使用 L6 右手协议，不发送运动或配置写入。`--help` 可离线查看用法。

版本查询失败返回 2，角度查询失败返回 3，参数错误返回 1。

## 频率基准工具

以下目标仅支持 Linux，不属于默认构建。配置时启用 `LINKERBOT_BUILD_TOOLS=ON`，再显式构建：

```bash
cmake --build build --target l6_frequency_benchmark o6_frequency_benchmark --parallel 4
./build/tools/l6_frequency_benchmark --help
./build/tools/o6_frequency_benchmark --help
```

`--help` 不访问设备。L6 提供 `fake`、`fake-socketcan`、`socketcan`、`sdk` 层；
O6 提供 `fake`、`socketcan`、`sdk` 层。选择硬件层前先阅读参数说明，基准模式可能发送控制报文。
仅需进程内模拟时选择 `fake`。`l6_raw_session.cpp` 是共享支持代码，不是独立命令行程序。

报告应记录完整命令、层、源码版本、构建配置、接口、速率、持续时间及错误。
主机调度、固件和适配器均会影响吞吐，模拟速率不代表真实控制频率保证。
Windows 使用维护中的只读示例和[适配器指南](../docs/zh/connections/windows-adapters.md)，不使用这些 Linux 工具。
