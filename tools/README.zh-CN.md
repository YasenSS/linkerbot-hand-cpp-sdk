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
