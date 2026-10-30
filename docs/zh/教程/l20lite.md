# L20Lite 功能文档

L20Lite 使用经典 CAN，包含 10 个关节。
本页以 C++ `HandDevice` 为常用操作入口，示例按当前 C++ SDK 接口编写。

## 目录

- [使用方式](#usage)
- [关节顺序](#关节顺序)
- [身份识别](#identity)
- [能力查询](#capabilities)
- [角度控制](#angle-control)
- [速度控制](#speed-control)
- [加速度设置](#acceleration)
- [传感器数据读取](#sensor-reading)

<a id="usage"></a>

## 使用方式

先按[准备接口](../连接/准备通信接口.md)配置环境，选择[自动连接](../连接/自动连接.md)
或[手动连接](../连接/手动连接.md)。下面是 Linux SocketCAN 的完整只读程序，
提供本页后续片段使用的 `hand`、chrono 字面量和输出/等待函数。
Windows 连接时替换设备构造部分，传入已确认的 adapter factory；功能片段保持相同。

```cpp
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <linkerbot/hand/api/device.hpp>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

template<class Values>
void print_values(const Values &values) {
    for (const auto value : values) {
        std::cout << +value << ' ';
    }
    std::cout << '\n';
}

void check_sample(const SampleMeta &meta) {
    if (!meta.valid) {
        throw std::runtime_error("样本不可用");
    }
    if (meta.stale) {
        std::cerr << "样本已超过新鲜度窗口，请按业务要求重新读取\n";
    }
}

void wait_command(const CommandHandle &command) {
    if (!command.wait_for(2s)) {
        throw std::runtime_error("等待超时；命令未取消，不自动重发");
    }
    if (const auto error = command.error()) {
        std::rethrow_exception(error);
    }
    const auto stage = command.stage();
    if (stage != CommandStage::kAcknowledged &&
        !(stage == CommandStage::kDispatched && command.ack_expectation() == AckExpectation::kNone)) {
        throw std::runtime_error("命令未成功完成，可能已被替换或取消");
    }
}

void print_sample(const RawVectorSample &sample) {
    check_sample(sample.meta);
    print_values(sample.raw);
}

void print_field(const ForceSample &force, const std::string &name) {
    const auto *field = force.field(name);
    if (!field) {
        std::cout << name << ": " << force.status(name) << '\n';
        return;
    }
    check_sample(field->meta);
    for (size_t finger = 0; finger < field->raw.size(); ++finger) {
        if (!field->raw[finger]) {
            std::cout << "finger " << finger << ": 无数据\n";
            continue;
        }
        const auto &values = *field->raw[finger];
        for (size_t point = 0; point < values.size(); ++point) {
            if (finger < field->valid_values.size() && field->valid_values[finger] &&
                point < field->valid_values[finger]->size() && !(*field->valid_values[finger])[point]) {
                std::cout << "invalid ";
            } else {
                std::cout << values[point] << ' ';
            }
        }
        std::cout << '\n';
    }
}

int main() try {
    HandDeviceConfig config;
    config.model = Model::kL20Lite;
    config.side = HandSide::kLeft; // 改为实际侧别
    config.interface_name = "can0"; // 此完整程序使用 Linux SocketCAN
    HandDevice hand(config);
    std::exception_ptr failure;
    try {
        hand.open();
        // 将下文所需的功能片段放在这里；不要一次执行整篇所有控制示例。
        const auto angles = hand.get_angles(1s);
        check_sample(angles.meta);
        print_values(angles.raw);
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        hand.close();
    } catch (const std::exception &error) {
        if (!failure) {
            throw;
        }
        std::cerr << "关闭连接失败：" << error.what() << '\n';
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

下文普通 C++ 片段分别放到 `hand.open()` 成功之后、`hand.close()` 之前，
并保留这些辅助函数；它们不是独立程序，也不应一次执行全部控制操作。
专用 Manager 小节另外提供独立程序。

统一 setter 和 `enable()` / `disable()` 等离散操作返回 `CommandHandle`。
`wait_command()` 先等待终结，再检查错误和终态：`kAcknowledged` 或无 ACK 的 `kDispatched`
才按对应协议视为通信完成。被替换、取消或失败都不是成功；等待超时不取消命令，不自动重发。
机械到位和控制效果需要业务另外验证。

样本先检查 `meta.valid`，并按 `meta.stale` 判断是否过期；缓存读数不是实时测量。
关闭异常时长期应用应保留同一对象安排清理重试，不能直接另开连接。

## 关节顺序

SDK 按以下顺序读取和设置关节数据。

| 索引编号 | 关节名称 | 关节 |
| ---: | --- | --- |
| 1 | `thumb_flex` | 拇指弯曲 |
| 2 | `thumb_roll` | 拇指翻滚 |
| 3 | `index_flex` | 食指弯曲 |
| 4 | `middle_flex` | 中指弯曲 |
| 5 | `ring_flex` | 无名指弯曲 |
| 6 | `pinky_flex` | 小指弯曲 |
| 7 | `index_abd` | 食指侧摆 |
| 8 | `ring_abd` | 无名指侧摆 |
| 9 | `pinky_abd` | 小指侧摆 |
| 10 | `thumb_rotation` | 拇指旋转 |

表格序号从 1 开始；C++ 向量下标为 `0..9`，以能力表 `joints[index].name` 为准。

<a id="identity"></a>

## 身份识别

`get_device_info(1s)` 主动读取并核验身份；C++ 返回结构体，按字段查看而不是直接打印整个对象。
`get_identity()` 只读取当前连接代的身份缓存。

```cpp
const auto info = hand.get_device_info(1s);
std::cout << "序列号：" << info.serial_number.value_or("设备未报告") << '\n';
std::cout << "产品：" << info.product_code.value_or("设备未报告") << '\n';
```

<a id="capabilities"></a>

## 能力查询

`get_capabilities()` 不发送控制命令。用能力表确认当前型号/协议是否支持操作，
尤其是同一型号的不同固件、协议和传感器。关节下标从 0 开始；下表序号从 1 开始。

```cpp
const auto caps = hand.get_capabilities();
std::cout << caps.serialize() << '\n';
for (const auto &joint : caps.joints) {
    std::cout << joint.index << ": " << joint.name << " raw="
              << joint.raw_angle_range.minimum << ".." << joint.raw_angle_range.maximum << '\n';
}
```

<a id="angle-control"></a>

## 角度控制

原始角度为 `0..255`，默认百分比与原始值同向。具体机械姿态按关节、侧别及实际角度映射判断，
不能把侧摆或旋转轴的两个端点简单等同于“弯曲/伸直”。

### 角度读取

`AngleSample::raw` 是 `vector<double>`；`percent` 是逐关节的 `optional<double>`，
无法换算的百分比不能补成 0。

```cpp
const auto sample = hand.get_angles(1s);
check_sample(sample.meta);
print_values(sample.raw);
for (const auto &percent : sample.percent) {
    if (percent) {
        std::cout << *percent << ' ';
    } else {
        std::cout << "unavailable ";
    }
}
std::cout << '\n';
```

### 原始角度设置

`set_angles()` 接收 10 个 `int32_t`，按关节表顺序排列。下面读取当前值作为示例目标，
实际业务中替换为已确认且在范围内的目标；仍会发送控制帧，不保证设备不会运动。

```cpp
const auto sample = hand.get_angles(1s);
check_sample(sample.meta);
std::vector<int32_t> targets(sample.raw.begin(), sample.raw.end());
wait_command(hand.set_angles(targets));
const auto after = hand.get_angles(1s);
check_sample(after.meta);
print_values(after.raw);
```

### 百分比角度设置

`set_percent_angles()` 接收 10 个 `float`，范围 `0..100`。`50.0F` 仅表示各轴映射中点，
不是经过验证的通用安全姿态；应按实际任务替换。

```cpp
std::vector<float> targets(10, 50.0F);
wait_command(hand.set_percent_angles(targets));
```

<a id="speed-control"></a>

## 速度控制

### 实时速度读取

主动读取当前速度；实际速度与设定值不同。

```cpp
print_sample(hand.get_speeds(1s));
```

### 速度设置

原始设置范围为 `0..255`。

```cpp
wait_command(hand.set_speeds(std::vector<double>(10, 100.0)));
```

`get_configured_speeds()` 只读完整写回复缓存，不主动查询。连接重建后缓存需要重新建立。

```cpp
print_sample(hand.get_configured_speeds(1s));
```

<a id="acceleration"></a>

## 加速度设置

### 加速度设置

`set_accelerations()` 接收 10 个原始计数，范围 `0..254`，不能直接当作物理加速度。

```cpp
wait_command(hand.set_accelerations(std::vector<double>(10, 100.0)));
```

### 加速度配置读取

L20 Lite 只读取完整写回复缓存；须在同一连接先设置加速度。

```cpp
print_sample(hand.get_configured_accelerations(1s));
```

<a id="sensor-reading"></a>

## 传感器数据读取

### 电流读取

`get_currents()` 读取实际电流原始计数，和电流上限设置不同；没有可靠比例时不标成 mA。

```cpp
print_sample(hand.get_currents(1s));
```

### 温度读取

本型号温度能力表声明单位为摄氏度；仍须检查样本有效性。

```cpp
print_sample(hand.get_temperatures(1s));
```

### 触觉点阵读取

C++ 使用 `get_force(vector<string>{"fn"}, timeout)` 选择字段，并通过 `field("fn")` 读取，
不是 Python 的 `force.fn.raw`。`raw` 中每个可选向量对应一个手指；`nullopt` 表示缺失，
`valid_values` 中的 `false` 表示该值无效。前置函数 `print_field()` 按这些信息显示，不补零、不自行求和。

```cpp
const auto force = hand.get_force(std::vector<std::string>{"fn"}, 1s);
print_field(force, "fn");
```

默认点阵形状为 5 个手指，每指 12×6。返回字段及 `force.tactile` 保留实际数据；以当前能力和固件为准。

已验证的 V4.6.10 固件不支持部分触觉标量查询。先查看连接后的能力表和返回状态，
缺失字段或抛出的不支持异常不应被解释为数值 0。

### 法向汇总读取

请求 `fns` 字段，仅在当前固件支持时执行。

```cpp
const auto force = hand.get_force(std::vector<std::string>{"fns"}, 1s);
print_field(force, "fns");
```

### 切向汇总读取

请求 `fts` 字段，仅在当前固件支持时执行。

```cpp
const auto force = hand.get_force(std::vector<std::string>{"fts"}, 1s);
print_field(force, "fts");
```

### 切向方向读取

请求 `fds` 字段，仅在当前固件支持时执行。 `fds` 的 255 表示方向未定义，应结合有效性掩码判断。

```cpp
const auto force = hand.get_force(std::vector<std::string>{"fds"}, 1s);
print_field(force, "fds");
```

### 接近值读取

请求 `px` 字段，仅在当前固件支持时执行。

```cpp
const auto force = hand.get_force(std::vector<std::string>{"px"}, 1s);
print_field(force, "px");
```

### 默认触觉读取

不指定字段时使用 SDK 的默认字段策略，不代表所有固件都能提供所有字段。

```cpp
const auto force = hand.get_force(1s);
for (const auto &field : force.fields) {
    print_field(force, field.name);
}
```

更多功能、固件限制和物理单位说明见[各型号能力说明](../../model-capabilities.md)。

[返回文档总览](../../README.md)
