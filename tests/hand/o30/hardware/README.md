# O30 HOP 0.0.4 实机验收

[English summary](README.en.md) | [中文原始记录](README.md)

本文为删除 O30 运动时间、扭矩上限功能之前的历史验收记录；其中接口及配置描述仅对应记录的 SDK 版本。

`verify_fixes.cc` 是独立验收程序，未加入默认离线 CTest。
针对 2026-09-17 测试的 O30 右手（应用 1.1.0、构建时间
2026-09-14 10:53:01），检查身份、故障、错误历史、五指触觉和稀疏 API。

程序会将食指指尖目标改变最多 12/255 行程、读取实际位置，并检查其余
19 关节目标不变。结束时恢复完整初始关节目标和传感器选择。
运行前确保手周围没有障碍；不要与其他控制程序同时运行。

从仓库根目录编译（通信仓库位置不同时调整路径）：

```bash
cmake -B build -S . -DLINKERBOT_COMM_SOURCE_DIR=../linkerbot-transport-cpp
cmake --build build --target tests -j4
c++ -std=c++17 -O2 -pthread -Iinclude -I../linkerbot-transport-cpp/include \
  tests/hand/o30/hardware/verify_fixes.cc \
  build/liblinkerbot-cpp-sdk.a build/_deps/linkerbot-transport-cpp/liblinkerbot-comm.a \
  -ldl -o build/o30_verify_fixes
./build/o30_verify_fixes can0 /tmp/o30-verify-frames.log
```

接口需已配置标准 CAN FD、1 Mbps、无 BRS，请求 ID 0x001，应答 0x401。
成功返回 0，日志包含每项验收及恢复结果。退出 1 时查看具体失败项；若
恢复时通信中断，应人工核对硬件状态。该程序不是任意设备的通用诊断工具。

本次修复的公开行为：

- HOP 0.0.4 的 `set_sparse` / `set_sparse_joints` 使用 MI 0x01 单槽写入，
  不再发送实测不兼容的 MI 0x30；多关节操作分帧执行，不保证原子性。
- 触觉按 HOP 版本选择元数据布局，0.0.4 选择器为 SI 0x6E，支持描述符校验、
  `read_data()` 完整读取，以及最大 767 字节的三块数据区。
- `CommErrorData::history` 改为 `std::vector<uint8_t>`，保留实际 15/16 字节。
  显式依赖固定数组类型的调用方需要更新并重新编译。
- 延续主分支的故障 MI 0x0C、限流写 CTRL 0x84；旧使能读写和故障写入接口
  继续抛出 `std::logic_error`，零发帧。有效关节状态不冒充使能状态。
- 身份仍按主分支探测 225/249 字节布局，增加短响应检查；事务层检查 FD、
  RTS 和片段覆盖，不把丢片后的零填充当作真实数据。

`select()` 与 `read_data()` 是两次独立调用。多线程使用时，应用应将选指和
读取作为一组串行执行。完整触觉数据是原始字节，没有新增受力标定或精度保证。

## 最新 master 基线上的提交前验证

- 默认 CTest 5/5 通过，包含 480 个离线通信用例、15,173 条断言。
- O30 专项离线回归 92 用例、978 条断言通过。
- 29 个只读硬件用例、182 条断言通过，含身份与触觉完整长度检查。
- 完整验收在运动前退出：当前设备故障不为零，因此未执行本轮动作和选指写入。
  原始故障：thumb_roll、thumb_yaw、index_root2、thumb_tip 为 0x02，
  index_yaw 为 0x08，后者温度原始计数为 81。位定义提示堵转/过温，未以
  外部仪器独立确认实际原因。日志见 evidence-20260917。
- 本次会话此前在修复后的工作区已验证五指 72 字节读取和稀疏食指动作，
  但最新 master 合并版本的完整动作验收尚未通过，不能将前次结果冒充本轮通过。
