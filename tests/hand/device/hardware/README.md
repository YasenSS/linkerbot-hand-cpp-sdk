# `HandDevice` 硬件测试

这里放统一 `linkerbot::hand::HandDevice` API 的真实设备测试。它与 `tests/hand/l30/` 等目录中的型号专用测试不同：控制命令和验收读回通过 `HandDevice`。O30 角度用例仅在准备阶段只读查询型号专用的原目标，因为 `HandDevice` 当前只公开实际角度；该查询不发送运动命令。

## 建立 CAN 连接

连接适配器和手、接通供电后，在终端配置并启用 SocketCAN 接口。以下以 `can0`
为例；实际接口名应与测试环境变量 `CAN_INTERFACE` 一致。重新配置已启用的接口时，
先执行 `sudo ip link set can0 down`。

L6、O6、L20、L20 Lite 使用经典 CAN：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000
sudo ip link set can0 up
```

若该接口之前用于 CAN FD，先关闭接口，并在上面的配置命令末尾加 `fd off`。

L30 使用 CAN FD：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can0 up
```

O30 也需要 CAN FD，但使用标准帧且不使用 BRS；速率按适配器与固件配置，见
[SDK 连接说明](../../../../docs/usage.md#连接-can-总线)。

Windows 的 PCAN Classic/FD、MiniCANFD 参数、PowerShell 命令及证据要求见
[Windows 真机验收步骤](../../../../docs/hardware-acceptance/windows.md)。同一套测试通过
`HAND_DEVICE_BACKEND=pcan|minicanfd` 选择真实适配器，SocketCAN 仍是 Linux 默认值。

配置后检查接口状态和速率，确认接口已启用，再运行实机测试：

```bash
ip -details -statistics link show can0
```

USB 适配器拔插后若接口重新出现但未启用，需重新执行对应配置和启用命令。

## 目录和命名

按功能拆分文件，通过环境变量选择型号：

```text
hardware/
  test_lifecycle.cpp       # open/close/health/recover
  test_identity.cpp        # verify_identity/get_device_info
  test_control.cpp         # enable/disable/faults
  test_angles.cpp          # raw/percent angle command and readback
  test_parameters.cpp      # speed/current/configuration APIs
  test_telemetry.cpp       # get_* queries and snapshot
  test_polling.cpp         # polling, wait_for_snapshot, stream
  test_force.cpp           # force selectors, descriptors, normal resultant
  test_validation.cpp      # all closed-state APIs, numeric validation, capability rejection
  test_command_contracts.cpp # constructor paths, ACK, replacement, cancellation, no replay
  test_disconnect.cpp      # operator-assisted physical disconnect/reconnect
  test_load.cpp            # opt-in >=10 minute read-only load and CSV metrics
```

测试用例标签包含 `hardware`、`device` 和功能：

```cpp
TEST_CASE("HandDevice hardware angle readback", "[hardware][device][angles][read-only]")
```

这样可以只运行某个型号或某个功能：

```bash
HAND_DEVICE_MODEL=l30 ./build/tests/tests "[hardware][device]"
HAND_DEVICE_MODEL=l30 ./build/tests/tests "[hardware][device][angles][read-only]"
```

当前通用硬件用例通过 `HAND_DEVICE_MODEL` 选择型号，没有 `[l6]`、`[l30]` 等
型号标签。`HAND_DEVICE_SIDE` 配置经典 CAN 左右手，`HAND_DEVICE_ADDRESS` 与
`HAND_DEVICE_HOST_ADDRESS` 配置 L30 的 NodeID/HostID，`CAN_INTERFACE` 指定接口。

## 公开接口覆盖与运行分组

`HandDevice` 有 57 个普通公开方法名、59 个签名，另有 3 种构造路径。
本目录共 37 个测试用例，通过能力表适配六种型号。
当前各入口均有测试调用，支持接口的正常路径可能需要下文的开关或已知配置基准；
按型号不支持的接口必须抛出
`UnsupportedOperationError`，不会静默略过。方法覆盖不等于每种设备已完成验收。

| 接口组 | 检查位置 |
| --- | --- |
| open/close/is_open/recover、能力和健康 | lifecycle、validation、command_contracts |
| verify_identity/get_device_info/get_identity/get_verified_capabilities | identity、validation |
| set_angles/set_percent_angles/get_angles | angles、validation、command_contracts |
| 其余 13 个数值 setter 与对应配置 getter | parameters、validation；清单位于 hand_device_api_catalog.hpp |
| clear_faults/enable/disable/save_parameters | control、validation |
| 17 个向量 getter、get_faults/get_snapshot | telemetry、validation |
| 两种 get_force、get_normal_force_resultant | force、validation |
| 两种 start_polling、stop_polling、wait_for_snapshot、stream_handle/stop_stream | polling、lifecycle、validation |
| TransportFactory / shared Session 构造 | fixture、command_contracts；默认 config 仅限 Linux SocketCAN |

先设置当前型号和总线配置，以 L30 为例：

```bash
export HAND_DEVICE_MODEL=l30 CAN_INTERFACE=can0
export HAND_DEVICE_ADDRESS=1 HAND_DEVICE_HOST_ADDRESS=0
./build/tests/tests "[hardware][device][read-only]"
./build/tests/tests "[hardware][device][validation]"
```

这两组不写正常目标或配置；身份刷新和恢复会重新建立 SDK 连接。
构造、恢复和传输故障注入用例使用所选真实适配器，覆盖 `TransportFactory` 与共享
Session。O30 为只读原目标查询复用同一 Session，避免再开独占通道；其他型号使用
延迟打开的 factory。O30 fixture 构造即建立 Session，不能将它当作零 I/O 构造验证。
原 Linux 各场次结果保持原记录，新增 Windows 入口尚需实际执行。
其余用例分别开启对应环境变量，避免一次筛选自动执行保存：

| 环境变量设为 1 | 用途 / 筛选 |
| --- | --- |
| `HAND_DEVICE_ENABLE_PARAMETER_TESTS` | 配置逐通道变更、读回与恢复；`[parameters][write]` |
| `HAND_DEVICE_ENABLE_CONTROL_TESTS` | 清故障、使能、失能；`[control][write]` |
| `HAND_DEVICE_ENABLE_SAVE_TESTS` | 保存参数；`[persistent]`，保存 ACK 不证明断电后持久化 |
| `HAND_DEVICE_ENABLE_FAULT_TESTS` | 丢弃回复、发送失败、阻塞驱动；`[commands][fault-injection]` |
| `HAND_DEVICE_ENABLE_MOTION_TESTS` | 角度运动，以及命令契约中的原姿态角度发送 |
| `HAND_DEVICE_ENABLE_DISCONNECT_TESTS` | 现场拔线与重连；`[disconnect][manual]` |
| `HAND_DEVICE_ENABLE_LOAD_TESTS` | 至少 600 秒只读角度负载；`[load][manual][read-only]`，需先冻结阈值 |
| `HAND_DEVICE_ENABLE_LIFECYCLE_STRESS` | 同进程至少 100 次开关和穿插恢复；`[lifecycle][manual][read-only]`，需另采集资源趋势 |

参数用例对每个支持接口、每个通道执行变更与恢复，正常和异常退出均尝试恢复。
可查询的配置读取设备原值；只有命令缓存或尚无写应答的配置必须提供已知基准，
否则该 section 明确 SKIP。L30 需要 `HAND_DEVICE_BASELINE_SPEEDS` 和
`HAND_DEVICE_BASELINE_CURRENT_LIMITS`，各为 17 个逗号分隔的协议原始值。
其他基准变量见 `hand_device_api_catalog.hpp`；这些值必须由现场确认，不提供猜测默认值。
缓存测试会注明 `SDK-command-cache`，它只验证 SDK 缓存；`device-write-response`
验证设备写回复，`device-query` 才是独立查询。禁止将三种结果混写为实测配置生效。
L20 的速度和电流限制没有配置读回接口，需提供各 16 个值的
`HAND_DEVICE_BASELINE_SPEEDS`、`HAND_DEVICE_BASELINE_CURRENT_LIMITS`；测试注明
`transport-receipt-only`，只确认各通道的命令发送和基准恢复命令，不证明配置读回。
缓存恢复用例还检查旧代配置失效，重新写入已知基准后才能获得新代有效样本。
若原配置没有记录，须明确建立新的测试基准并记录结束状态，不能写成恢复原值。
本次 L30 使用所有关节速度 raw=20、电流限制 raw=60 的新基准；不写入 EEPROM。
L6 V2.4.5 的速度和电流限制实测存在 1 raw 的取整差异，参数用例使用至少 3 raw
的步幅和 1 raw 容差，仍能拒绝未改变的配置。命令回复可能包含写入前的值；
对不能主动查询的配置，仅在上一次命令成功结束后，追加一次相同写入获取确认回复。

O6 的速度和电流限制均使用设备写回复，需提供各 6 个值的
`HAND_DEVICE_BASELINE_SPEEDS`、`HAND_DEVICE_BASELINE_CURRENT_LIMITS`。
O6 V1.3.2 的单字节速度查询实测会把速度变为 0，统一 getter 已改为只读写回复，
不会发送该查询；首次写入前缓存不可用，不能据此得知原速度。
缺 ACK 测试优先使用速度或电流限制，可使用这些已知基准，避免为此写入高级参数。

L20 Lite 需要各 10 个值的 `HAND_DEVICE_BASELINE_SPEEDS` 和
`HAND_DEVICE_BASELINE_CURRENT_LIMITS`；本轮采用速度 20／限流 60 新基准。
速度配置写回复与实时速度独立，实时查询不会填充或覆盖配置缓存。
`V4.6.10` 的速度写回复存在 1 raw 取整，测试步幅至少 3 raw、容差 1 raw；
限流保持默认精确检查。该固件组合支持五指 `fn` 矩阵，独立标量字段
`fns/fts/fds/px` 与法向合力按已验证能力明确拒绝，计为能力拒绝检查。

命令契约用例保持当前合法姿态，通过真实链路验证 receipt 和参数复制；初始姿态
越界时须先完成角度基准准备。故障注入是在所选真实适配器外包一层测试传输，
检查 ACK 超时、失败、替换、取消及旧目标不重放，不计为真实拔线验收。
O30 用设备原有角度目标准备整手命令，避免把反馈偏差累积写回目标；静止命令
检查原目标未变、实际姿态保持稳定。角度运动仍要求反馈接近新目标，恢复时
确认原目标读回一致，并检查实际姿态回到测试前位置，两项使用各自的基准。
控制检查的公共 `require_done` 要求成功终态、发送帧数和单调 receipt 历史。
参数持久化的物理效果仍需现场检查，不凭 ACK 判定完成。

```bash
# 传输失败、超时、排队替换和关闭/恢复取消；角度发送保持当前合法姿态。
HAND_DEVICE_ENABLE_FAULT_TESTS=1 HAND_DEVICE_ENABLE_MOTION_TESTS=1 \
  ./build/tests/tests "[hardware][device][commands]"
# 参数组需先提供上述已知基准。
HAND_DEVICE_ENABLE_PARAMETER_TESTS=1 \
  ./build/tests/tests "[hardware][device][parameters][write]"
```

事件流与 `get_snapshot()` 共享元信息补齐逻辑，检查当前连接代次、样本序号及新鲜度。
`HandDevice hardware stream payload and bounded queue` 已在 L30 上通过。
L30 不支持 `save_parameters()`，该用例验证明确拒绝，不代表实施了参数持久化。

真实拔线用例需要另一个终端写标记文件，运行前先清除旧内容：

```bash
export HAND_DEVICE_DISCONNECT_SIGNAL=/tmp/hand-device-disconnect.signal
printf 'ready\n' > "$HAND_DEVICE_DISCONNECT_SIGNAL"
HAND_DEVICE_ENABLE_DISCONNECT_TESTS=1 \
  ./build/tests/tests "HandDevice hardware physical disconnect and reconnect"
# 按测试提示实际断开后，在另一个终端执行：
printf 'disconnected\n' > /tmp/hand-device-disconnect.signal
# 同一只手重连，并恢复原 CAN 接口配置后执行：
printf 'reconnected\n' > /tmp/hand-device-disconnect.signal
```

每步默认等待最多 300 秒；可通过 `HAND_DEVICE_DISCONNECT_WAIT_MS` 配置为
1000–1800000 毫秒，仅改变现场操作等待，不改变通信超时。
标记只表示操作完成，仍须实际读取失败、恢复成功、身份一致
及新代反馈到位。该用例不发送运动目标。报告继续更新型号目录中的一份 Markdown，
失败、跳过与未执行均不能写成通过。



## 使用说明

以下以 L6 右手、`can0`、默认地址 `0x27` 为例。先按上面的
[建立 CAN 连接](#建立-can-连接) 配置接口，再在 SDK 根目录的同一终端依次执行。

**1. 编译并查看测试用例**

```bash
cd /path/to/linkerbot-hand-cpp
cmake --build build --target tests --parallel 4
./build/tests/tests --list-tests "[hardware][device]"
```

`cd` 路径按实际源码位置替换；角度用例可单独通过
`--list-tests "[hardware][device][angles]"` 查看，应列出 6 个用例。

**2. 配置当前设备**

```bash
export HAND_DEVICE_MODEL=l6
export HAND_DEVICE_SIDE=right
export HAND_DEVICE_ADDRESS=0x27
export CAN_INTERFACE=can0

# 清除之前 L30、单关节或初始姿态修正测试留下的设置
unset HAND_DEVICE_HOST_ADDRESS
unset HAND_DEVICE_ANGLE_JOINT
unset HAND_DEVICE_ANGLE_NORMALIZE_START

export HAND_DEVICE_ANGLE_STEP_PERCENT=5
export HAND_DEVICE_ANGLE_TOLERANCE_PERCENT=2
export HAND_DEVICE_ANGLE_SETTLE_MS=5000
```

其他手型使用同一套测试，按实际设备修改型号、侧别和地址。L30 使用
`HAND_DEVICE_MODEL=l30`，按实际设置 NodeID/HostID，并清除 `HAND_DEVICE_SIDE`；
CAN FD 配置见上文。设备地址改过时，不能继续使用默认值。

**3. 先运行只读和输入校验测试**

```bash
./build/tests/tests "[hardware][device][read-only]"
./build/tests/tests "[hardware][device][validation]"
```

**4. 确认运动环境后运行角度测试**

```bash
HAND_DEVICE_ENABLE_MOTION_TESTS=1 \
  ./build/tests/tests "[hardware][device][angles]" --order decl
```

此命令运行 `test_angles.cpp` 的全部 6 个用例，独立与综合运动用例均执行。
不设置 `HAND_DEVICE_ANGLE_JOINT` 时覆盖全部关节，L6 应显示 `joints=6/6`；
L6 没有使能功能，测试按能力声明处理。现场确认对应关节运动、无卡顿且恢复姿态。
O30 单关节用例在运动前只读设备原有 20 个角度目标，并保留其他关节的目标；
被测关节检查实际角度到位和恢复，其他关节检查是否出现大幅偏移。若原有目标与
实际反馈相差超过 10% 行程，则在发送运动命令前失败。反馈未到位仍记为失败；
如需确认其他关节的独立运动，应分别选择其关节序号运行。

若只运行全关节综合用例，将筛选条件改为 `"HandDevice hardware full angle acceptance"`。
其他功能按上面的运行分组选择标签与开关；常用功能验收不要求执行
恢复出厂、参数持久化和高级调参。

**5. 判断结果并记录报告**

`All tests passed` 表示实际执行的检查通过；`SKIPPED` 表示未执行，
`No test cases matched` / `No tests ran` 表示未找到测试，均不能记为通过。
失败时先根据输出定位问题，修复后重新运行对应用例。
每种手型只维护一份 `docs/hardware-acceptance/<型号>/report.md`，记录环境、
运行命令、结果和人工观察；真实断连与恢复按上面的拔线用例另行执行。

## 安全要求

- 确认 `can0`、NodeID/Side 等配置与真实设备一致。
- 运行前确认急停、机械行程和周围环境。
- 会让设备运动的用例必须使用有限幅度、可恢复的姿态，并在结束时回到安全姿态。
- 不要把硬件测试加入默认 `ctest`；默认测试只运行 `FakeTransport` 的 `[comm]` 用例。

当前 `tests/hand/device/` 下的 `test_device.cpp` 和 `test_unified_snapshot.cpp` 仍是离线测试，使用 `FakeTransport`，不属于本目录的硬件测试。

## SLCAN 原生串口（实验）

除 SocketCAN 外，实机用例也可通过 Windows COM 或 Linux `/dev/...` 连接。
需要配套含 `LINKERBOT_COMM_SLCAN_FD=1` 的 comm；确认固件协议后配置，
不需要 `slcand`，不建立 `can0`。下面是 O30 只读示例（端口、侧别和地址按实际修改）：

```bash
HAND_DEVICE_MODEL=o30 HAND_DEVICE_SIDE=right HAND_DEVICE_ADDRESS=1 \
HAND_DEVICE_BACKEND=slcan HAND_DEVICE_CHANNEL=/dev/ttyACM0 \
HAND_DEVICE_SLCAN_PROTOCOL=canable-fd HAND_DEVICE_SERIAL_BAUD=115200 \
HAND_DEVICE_SERIAL_TIMEOUT_MS=500 HAND_DEVICE_BITRATE=1000000 \
HAND_DEVICE_DATA_BITRATE=5000000 \
./build/tests/tests "HandDevice hardware angle readback"
```

L30 改型号、节点和 host 地址；Classic 四型号不传 DATA_BITRATE，
Lawicel 固件使用 `HAND_DEVICE_SLCAN_PROTOCOL=lawicel-classic`，
CANable 固件继续用 canable-fd。可选 `HAND_DEVICE_SERIAL_STARTUP_MS=100`。
Windows 设置相同环境变量，CHANNEL 改为实际 COM 口。Linux 用户需有串口读写权限。
运动仍遵循本页安全要求和 `HAND_DEVICE_ENABLE_MOTION_TESTS=1` 的显式开关。
CANable 无串口 ACK，命令写入完成只证明串口驱动已收取字节，验收须以设备反馈为准。
SLCAN 实物和 Windows 新扩展尚待验收，详情见 [接入文档](../../../../docs/windows-adapters.md)。
