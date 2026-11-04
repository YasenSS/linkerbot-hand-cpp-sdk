# 构建与验证指南

[English](../../en/guides/validation.md) · [文档索引](../README.md)

## 明确选择依赖

transport 使用项目 README 与 `.github/workflows/ci.yml` 固定的完整提交号。
显式设置 `LINKERBOT_COMM_SOURCE_DIR` 可以选择源码目录；将其设为空字符串，
并配置 `CMAKE_PREFIX_PATH`，则使用已安装的 `linkerbot-comm` 包。
未显式设置时，CMake 可能自动选中相邻的 `linkerbot-transport-cpp` 目录。
测试报告应记录实际依赖提交和构建配置。

## 运行离线测试

在 SDK 源码根目录执行，前提是已安装 Catch2 3，且相邻目录是固定版本的 transport：

```bash
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release \
  -DLINKERBOT_COMM_SOURCE_DIR=../linkerbot-transport-cpp \
  -DLINKERBOT_BUILD_TESTS=ON -DLINKERBOT_BUILD_EXAMPLES=OFF \
  -DLINKERBOT_BUILD_TOOLS=OFF -DLINKERBOT_COMM_BUILD_TESTS=OFF \
  -DLINKERBOT_ALLOW_DEPENDENCY_DOWNLOADS=OFF
cmake --build build-tests --parallel 4 --target tests protocol-tests
ctest --test-dir build-tests --output-on-failure
```

测试程序不属于默认构建目标，运行 CTest 前必须显式构建。库编译成功不等于测试通过。
离线夹具验证协议、调度和生命周期约定，不能证明 USB 驱动、固件时序、机械精度或实机行为。
实际支持边界见[平台与硬件验证范围](../reference/compatibility.md)。

Windows 环境准备见 [Windows 指南](windows.md)。构建两个测试目标时增加
`--config Release`，运行 CTest 时增加 `-C Release`，不要混用 Debug 与 Release 库和运行库设置。

## 判断命令结果

setter 返回句柄表示本地接纳。逻辑执行结果应检查终态和错误；ACK 不代表机械到位。
物理批次证据通过 `send_results()` 查询，见 [API 指南](usage.md#查询命令的物理发送结果)。
超时或生命周期错误不能证明已进入驱动的写入没有发送。
结果不确定时，不要自动重发运动或持久化配置命令。

## 排查 CI 失败

先查失败步骤和第一条具体错误，记录 SDK 与 transport 提交、平台、编译器、构建配置、
测试名称及随机种子。Windows 附件包含构建日志、测试结果和打包证据。
区分编译失败、协议断言、专门的 deadline 测试，以及受主机调度影响的功能测试。
deadline 测试应保留严格边界；编码覆盖测试可以设置独立预算，不应为此修改生产默认值。

PR 作业执行离线测试和协议检查；main 分支还按工作流的平台条件执行安装、公共头文件、
文档示例和包检查。跳过不等于通过。历史实机报告只适用于其中记录的版本和环境。

## 本地编辑器配置

整个 `.vscode/` 目录均被忽略。机器路径和个人启动配置留在本地，项目维护的构建、
测试命令应放在 CMake、justfile 和文档中。
