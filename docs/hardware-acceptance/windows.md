# Windows 真机适配测试指南

本文用于已有 Linux 功能验收基础上的 Windows 适配检查，包含环境准备、构建、适配器配置、连通与单次角度测试、结果保存。
命令在 **同一个 x64 Developer PowerShell for VS 2022** 中依次执行；只有断连操作需要第二个终端。
当前组合状态见 [Windows 实测矩阵](README.md#windows-实测矩阵)，具体证据及限制见
[各型号报告](README.md)，不在操作指南中重复维护。

## 1. 本轮只验证 Windows 适配

**六种手的通用功能已经在 Linux 验收，本轮不在 Windows 逐项重测。**
Windows 要确认的是：相同 HandDevice 代码换用 Windows 驱动、DLL 或 COM 串口后，
能否发现对应手、打开连接、正确读取身份和角度，并完成一次简单的命令收发。

每个准备正式支持的“手型 + Windows 通信路径”做以下检查即可：

1. 枚举适配器、发现设备。
2. 分别通过发现连接和显式配置连接，读回身份与角度，结束后正常关闭。
3. 需要验证控制下行时，选一个关节做一次小幅 raw 角度测试，检查反馈并恢复；不跑全关节或 raw/percent 综合验收。

所有实际承诺的组合均检查枚举/发现、发现连接读取、显式连接读取及关闭释放；
单关节运动选测。型号、侧别、产品码/序列号、固件及角度数量须与真实设备相符。

| 型号 | CLI 型号参数 | 关节数 | 帧格式 | 适用 Windows 路径 |
| --- | --- | --- | --- | --- |
| L6 | `l6` | 6 | Classic CAN | PCAN Classic、SLCAN Classic |
| O6 | `o6` | 6 | Classic CAN | PCAN Classic、SLCAN Classic |
| L20 Lite | `l20lite` | 10 | Classic CAN | PCAN Classic、SLCAN Classic |
| L20 | `l20` | 16 | Classic CAN | PCAN Classic、SLCAN Classic |
| L30 | `l30` | 17 | 扩展 CAN FD | PCAN FD、MiniCANFD、SLCAN FD（CANable FD） |
| O30 | `o30` | 20 | 标准 CAN FD，无 BRS | PCAN FD、MiniCANFD、SLCAN FD（CANable FD） |

只测试实际准备承诺的路径；不把未接入的适配器或未执行的组合标为通过。
L30 使用实际 NodeID、HostID 0，O30 使用实际 HOP 请求 ID；标准与扩展 FD 分别验证。

SLCAN 记录具体固件协议：Lawicel Classic 和 CANable FD 不互相代替；若更换固件，先用代表手确认新固件的接入路径。
Windows 编译及自动离线检查对一组源码做一次，换手时不重跑。
USB 断连恢复可以按每种适配器/驱动/固件组合选一只代表手做一次，不作为每个手型的重复项目。
速度/限流写入、使能/清故障专项、全量遥测/轮询、命令故障注入、压力负载及高级功能均不列入本轮逐手必测项。
本轮结论写“Windows 适配连通/角度收发通过”，不写成所有通用功能或长期性能重新验收通过。

## 2. 准备电脑和设备

工具、运行库及打包验证见 [Windows 构建说明](../windows.md#构建-sdk)；驱动、DLL、
通道选择及固件要求见 [适配器接入](../windows-adapters.md)。仅准备本场使用的适配器。
核对 USB、供电、CAN H/CAN L、公共地和终端电阻，关闭其他控制程序并留出运动空间。
出现异常运动、卡住或明显发热时停止运动测试。Windows 不配置 `can0`，不运行 `ip link` 或 `slcand`。

## 3. 定位已通过离线检查的构建

先按 [构建说明](../windows.md#构建-sdk) 完成所选配置的构建及离线检查，再在源码根目录设置：

```powershell
$ErrorActionPreference = 'Stop'
$Build = (Resolve-Path '.\build-release').Path # 按实际构建目录修改
$Configuration = 'Release' # 使用 Debug 时同时修改配置和构建目录
$Tests = (Resolve-Path "$Build\tests\$Configuration\tests.exe").Path
$Read = (Resolve-Path "$Build\examples\$Configuration\windows_readonly.exe").Path
& $Read --help
if ($LASTEXITCODE -ne 0) { throw 'Read-only entry unavailable' }
& $Tests 'HandDevice hardware raw angle commands' --list-tests
```

同一源码及配置换手或换适配器不重复离线检查。只在已通过检查的配置上执行真机步骤；
其他配置若失败或被系统策略阻止启动，单独记录，不能标成双配置或发行包完整通过。
记录实际 hand/comm 完整 SHA 和工作区差异，不从旧报告推定当前源码及设备身份。

## 4. 设置当前手和适配器

### 4.1 当前手：每换一只手或一条路径，从这里重新设置

下面先以 L6 右手为例，修改前三项即可选择其他型号。地址必须来自实际设备配置；
L6 常见左/右地址为 `0x28` / `0x27`，不能据此推测其他手的实际地址。
L30 的地址是 NodeID，O30 的地址是 HOP 请求 ID；下文 FD 示例使用地址 `1`，L30 HostID 为 `0`。

```powershell
Get-ChildItem Env:HAND_DEVICE_* | ForEach-Object { Remove-Item "Env:$($_.Name)" }
$Model = 'l6'       # l6 / o6 / l20lite / l20 / l30 / o30
$Side = 'right'     # 按实际填写 left / right
$Address = '0x27'   # FD 示例改为实际节点/请求 ID，例如 '1'
$JointCount = @{ l6=6; o6=6; l20lite=10; l20=16; l30=17; o30=20 }[$Model]
if (-not $JointCount) { throw 'Unknown model' }
$IsFd = $Model -in @('l30', 'o30')
$Mode = if ($IsFd) { 'fd' } else { 'classic' }
$env:HAND_DEVICE_MODEL = $Model
$env:HAND_DEVICE_SIDE = $Side
$env:HAND_DEVICE_ADDRESS = $Address
if ($Model -eq 'l30') { $env:HAND_DEVICE_HOST_ADDRESS = '0' }
$env:HAND_DEVICE_BITRATE = '1000000'
```

以下三种适配器配置**只执行自己使用的那一段**。1M 仲裁速率和 5M FD 数据速率均须与手和适配器的实际配置一致。

### 4.2 PCAN Classic / PCAN FD

先枚举，再把实际通道填入 `$Channel`。FD 需要适配器明确支持 FD，不能把 Classic-only 通道用于 L30/O30。
FD 示例为 80 MHz、1M/5M 时序；须先确认所选设备支持该配置，修改速率时同步修改全部时序参数。

```powershell
$Backend = 'pcan'
$Library = 'C:\Vendor\PCANBasic\x64\PCANBasic.dll'
& $Read --list --backend pcan --library $Library
if ($LASTEXITCODE -ne 0) { throw 'PCAN enumeration failed' }
$Channel = 'PCAN_USBBUS1' # 用上一步结果，不按手型号猜通道
$env:HAND_DEVICE_BACKEND = $Backend
$env:HAND_DEVICE_LIBRARY = $Library
$env:HAND_DEVICE_CHANNEL = $Channel
$Cli = @('--backend', $Backend, '--channel', $Channel, '--mode', $Mode,
  '--library', $Library, '--bitrate', $env:HAND_DEVICE_BITRATE)
if ($IsFd) {
  $env:HAND_DEVICE_DATA_BITRATE = '5000000'
  $Timing = @{ FD_CLOCK='80000000'; NOM_BRP='1'; NOM_TSEG1='63'; NOM_TSEG2='16'; NOM_SJW='16';
    DATA_BRP='1'; DATA_TSEG1='11'; DATA_TSEG2='4'; DATA_SJW='4' }
  foreach ($Entry in $Timing.GetEnumerator()) { Set-Item "Env:HAND_DEVICE_$($Entry.Key)" $Entry.Value }
  $Cli += @('--data-bitrate', $env:HAND_DEVICE_DATA_BITRATE,
    '--fd-clock', $env:HAND_DEVICE_FD_CLOCK, '--nom-brp', $env:HAND_DEVICE_NOM_BRP,
    '--nom-tseg1', $env:HAND_DEVICE_NOM_TSEG1, '--nom-tseg2', $env:HAND_DEVICE_NOM_TSEG2,
    '--nom-sjw', $env:HAND_DEVICE_NOM_SJW, '--data-brp', $env:HAND_DEVICE_DATA_BRP,
    '--data-tseg1', $env:HAND_DEVICE_DATA_TSEG1, '--data-tseg2', $env:HAND_DEVICE_DATA_TSEG2,
    '--data-sjw', $env:HAND_DEVICE_DATA_SJW)
}
```

### 4.3 MiniCANFD

本后端仅用于 L30/O30。通道使用枚举返回的原生设备索引加 `:0`；不要用 `device * 2 + channel` 推算。

```powershell
if (-not $IsFd) { throw 'MiniCANFD requires l30 or o30' }
$Backend = 'minicanfd'
$Library = 'C:\Vendor\MiniCANFD\x64\HCanbus.dll'
& $Read --list --backend minicanfd --library $Library
if ($LASTEXITCODE -ne 0) { throw 'MiniCANFD enumeration failed' }
$Channel = '0:0' # 按实际枚举结果修改
$env:HAND_DEVICE_BACKEND = $Backend
$env:HAND_DEVICE_LIBRARY = $Library
$env:HAND_DEVICE_CHANNEL = $Channel
$env:HAND_DEVICE_DATA_BITRATE = '5000000'
$Cli = @('--backend', $Backend, '--channel', $Channel, '--mode', 'fd', '--library', $Library,
  '--bitrate', $env:HAND_DEVICE_BITRATE, '--data-bitrate', $env:HAND_DEVICE_DATA_BITRATE)
```

### 4.4 SLCAN Classic / CANable FD

在设备管理器确认适配器的 COM 号，不对其他串口发送初始化命令。
`lawicel-classic` 使用 C/S/O、t/T，并等待串口 ACK，仅支持 Classic；
`canable-fd` 使用 CANable 的 d/D/b/B、Y2/Y5 扩展，可传 Classic 或 FD，**没有串口 ACK**。
产品名叫 CANable 不证明固件兼容；FD 数据速率仅支持 2M/5M。UART 波特率与 CAN 速率分别配置。

```powershell
$Backend = 'slcan'
$Channel = 'COM10'          # 实际适配器端口
$SlcanProfile = 'canable-fd'     # Lawicel 固件改成 'lawicel-classic'
$env:HAND_DEVICE_BACKEND = $Backend
$env:HAND_DEVICE_CHANNEL = $Channel
$env:HAND_DEVICE_SLCAN_PROTOCOL = $SlcanProfile
$env:HAND_DEVICE_SERIAL_BAUD = '115200' # 按固件修改
$env:HAND_DEVICE_SERIAL_TIMEOUT_MS = '500'
$env:HAND_DEVICE_SERIAL_STARTUP_MS = '100'
$Cli = @('--backend', $Backend, '--channel', $Channel, '--mode', $Mode,
  '--slcan-protocol', $SlcanProfile, '--bitrate', $env:HAND_DEVICE_BITRATE,
  '--serial-baud', $env:HAND_DEVICE_SERIAL_BAUD, '--serial-timeout-ms', '500', '--serial-startup-ms', '100')
if ($IsFd) {
  if ($SlcanProfile -ne 'canable-fd') { throw 'FD requires the canable-fd profile' }
  $env:HAND_DEVICE_DATA_BITRATE = '5000000'
  $Cli += @('--data-bitrate', $env:HAND_DEVICE_DATA_BITRATE)
}
```

SLCAN 不设置 `HAND_DEVICE_LIBRARY`；Classic 模式不设置 `HAND_DEVICE_DATA_BITRATE`。
CANable 的发送完成只表示字节交给串口驱动，仍须用手的身份、角度和命令反馈判断通信与运动结果。

## 5. 创建记录目录并确认连接

每场原始日志放在仓库外，最后只向每个型号的一份报告追加 Windows 结果。
先定义记录目录和运行函数；之后各组都会保存控制台、JUnit 和当时的测试环境变量。
同场重试时将 `-Name` 改为新名称（例如 `angles-retry1`），保留上次失败日志。

```powershell
$RunId = Get-Date -Format 'yyyyMMdd-HHmmss'
$Evidence = Join-Path $env:LOCALAPPDATA "LinkerBot\acceptance\$Model-$Backend-$RunId"
New-Item -ItemType Directory -Force $Evidence | Out-Null
Write-Host "Evidence: $Evidence"
git rev-parse HEAD | Set-Content "$Evidence\hand-sha.txt"
git status --short | Set-Content "$Evidence\hand-status.txt"
git diff | Set-Content -Encoding utf8 "$Evidence\hand.diff"
git -C ../linkerbot-transport-cpp rev-parse HEAD | Set-Content "$Evidence\comm-sha.txt"
git -C ../linkerbot-transport-cpp status --short | Set-Content "$Evidence\comm-status.txt"
git -C ../linkerbot-transport-cpp diff | Set-Content -Encoding utf8 "$Evidence\comm.diff"
Get-ComputerInfo | Out-File "$Evidence\windows.txt"
cmake --version | Out-File "$Evidence\cmake.txt"
Get-Item (Get-Command cl).Source | Select-Object FullName,
  @{Name='Version'; Expression={$_.VersionInfo.FileVersion}} | Out-File "$Evidence\msvc.txt"
if ($Backend -ne 'slcan') { Get-FileHash $Library -Algorithm SHA256 | Out-File "$Evidence\dll-sha256.txt" }

function Run-HandTest {
  param([string]$Name, [string]$Filter)
  $Xml = Join-Path $Evidence "$Name.xml"
  if ((Test-Path $Xml) -or (Test-Path (Join-Path $Evidence "$Name.log"))) {
    throw "Result already exists: $Name; choose a new Name for the retry"
  }
  Get-ChildItem Env:HAND_DEVICE_* | Sort-Object Name | Format-Table -AutoSize |
    Out-File (Join-Path $Evidence "$Name-env.txt") -Width 4096
  $PreviousPreference = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue' # Windows PowerShell 5.1：保留原生程序完整 stderr
    $global:LASTEXITCODE = $null
    & $Tests $Filter --order decl --abort --reporter console --reporter "JUnit::out=$Xml" 2>&1 |
      Tee-Object (Join-Path $Evidence "$Name.log")
    $Code = $global:LASTEXITCODE
  }
  finally { $ErrorActionPreference = $PreviousPreference }
  if ($null -eq $Code -or $Code -ne 0) { throw "$Name failed: exit=$Code; see $Evidence" }
  [xml]$Result = Get-Content -Raw -Encoding utf8 $Xml
  $Count = @($Result.SelectNodes('//testcase')).Count
  $Skipped = @($Result.SelectNodes('//testcase/skipped')).Count
  if ($Count -eq 0) { throw "${Name}: no test results" }
  if ($Skipped -gt 0) { throw "$Name skipped $Skipped results; check model and test switches before retrying" }
}

foreach ($Action in @('--list', '--discover', '--read', '--manual')) {
  $PreviousPreference = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue'
    $global:LASTEXITCODE = $null
    & $Read $Action @Cli --model $Model --side $Side --address $Address 2>&1 |
      Tee-Object (Join-Path $Evidence "$($Action.TrimStart('-')).log")
    $Code = $global:LASTEXITCODE
  }
  finally { $ErrorActionPreference = $PreviousPreference }
  if ($null -eq $Code -or $Code -ne 0) { throw "$Action failed; stop before motion" }
}
```

`--list` 只查元数据；`--discover` 查询身份；`--read` 通过发现创建连接并读身份/角度；
`--manual` 按指定型号/侧别/地址读取。四项都不发送运动目标。核对型号、侧别、序列号/产品码、固件和角度数量。
L30 的 CLI 使用 HostID 0，保持与本指南的 fixture 配置一致。

报告同时记录适配器型号、序列号/固件、Windows 驱动版本、DLL 版本及哈希、手的身份。
这些信息从设备回复、设备管理器或厂商组件属性获取；拿不到的字段明确写“未报告”，不要推测。

## 6. 按需做一个单关节角度测试

第 5 节通过即记录本组合的“连通/读取通过”。需要确认下行控制时，再运行下面**一个 case**。
不执行 `[hardware][device]` 全组、全关节 acceptance，也不重复 Linux 的参数、遥测和故障注入验收。

```powershell
$env:HAND_DEVICE_ANGLE_JOINT = '0' # 只选一个关节；可按实际修改并在报告注明
$env:HAND_DEVICE_ANGLE_STEP_PERCENT = '5'
$env:HAND_DEVICE_ANGLE_TOLERANCE_PERCENT = '2'
$env:HAND_DEVICE_ANGLE_SETTLE_MS = '5000'
$env:HAND_DEVICE_ENABLE_MOTION_TESTS = '1'
try { Run-HandTest -Name 'angle-smoke' -Filter 'HandDevice hardware raw angle commands' }
finally { Remove-Item Env:HAND_DEVICE_ENABLE_MOTION_TESTS -ErrorAction SilentlyContinue }
```

该用例读原姿态、改变选定关节、等待反馈接近目标，再恢复。命令仍携带整手目标向量，其他关节保持基准目标；
按型号能力处理使能/失能，L6/O6 不会被强行调用不支持的使能接口。
日志应显示 `joints=1/N`，例如 L6 为 `1/6`，并且目标反馈和恢复检查通过。
这足以为本轮提供真实命令与反馈经过 Windows 适配器的证据，不代表重新覆盖所有关节或百分比接口。

若失败在初始姿态检查，先确认原姿态、目标范围和故障状态。确需修正轻微越界时，
显式设置 `$env:HAND_DEVICE_ANGLE_NORMALIZE_START='1'` 后重跑；默认最多修正量程的 20%，
修正后的姿态成为恢复基准。修正可能涉及其他越界关节，默认不开启；不要为通过测试盲目放宽容差。
若不做运动，本组合只记录“连通/读取通过，运动未执行”。

## 7. 代表适配器的断连恢复检查（选做一次）

Windows 的 USB 驱动和 COM/DLL 资源恢复值得单独检查，但不要求每个手型重复。
同一适配器/驱动/固件组合选一只代表手即可；更换适配器或固件后结果不自动继承。
本项只验证实际运行的恢复路径，不包含长期负载或全部后端能力测试。

先运行下面命令，**看到控制台断开提示后再操作**。默认每步等待 300 秒，可按现场需要设置
`HAND_DEVICE_DISCONNECT_WAIT_MS`（1000–1800000 毫秒）；它不改变通信超时。

```powershell
$env:HAND_DEVICE_DISCONNECT_SIGNAL = Join-Path $Evidence 'disconnect.signal'
Set-Content -Encoding ascii $env:HAND_DEVICE_DISCONNECT_SIGNAL 'ready'
$env:HAND_DEVICE_ENABLE_DISCONNECT_TESTS = '1'
try { Run-HandTest -Name 'disconnect-usb' -Filter 'HandDevice hardware physical disconnect and reconnect' }
finally { Remove-Item Env:HAND_DEVICE_ENABLE_DISCONNECT_TESTS -ErrorAction SilentlyContinue }
```

第二个 PowerShell 中，把第一行替换为主终端显示的完整标记路径；按以下顺序操作：

```powershell
$Signal = Join-Path $env:LOCALAPPDATA 'LinkerBot\acceptance\本场目录\disconnect.signal'
# 1. 主终端提示断开后，拔下 USB 适配器，再执行：
Set-Content -Encoding ascii $Signal 'disconnected'
# 2. 等主终端确认读取失败并提示重连，接回同一适配器和同一只手。
# 3. 等 Windows 驱动恢复，确认设备/通道可用，再执行：
Set-Content -Encoding ascii $Signal 'reconnected'
```

用例不发送运动目标，必须看到读取失败、recover 成功、身份一致、新连接代次和新反馈。
通道/COM 号改变时按实际结果记录恢复失败，不能在场次中偷偷换配置。
每次重跑先将标记重新写为 `ready`，并改用新的日志名称。
如另外检查手部断电，保持 USB 连接、日志命名为 `disconnect-power`，不要与 USB 恢复混为同一个结论。

## 8. 判断结果和保存一份报告

- 第 5 节身份、角度和连接正常，记录“连通/读取通过”；不能仅凭 COM 口存在或 DLL 加载成功判断连通。
- 第 6 节实际运行且反馈/恢复通过，再记录“单关节角度收发通过”。未运动则如实写“未执行”。
- `No tests ran`、筛选不匹配或 `SKIPPED` 都不是通过；检查源码版本、型号和运动开关。
- CANable 无串口 ACK，写入完成不等于设备执行；角度用例的目标反馈检查才是运动证据。
- 断连恢复只在做过第 7 节的适配器组合记录；其他通用功能沿用既有 Linux 验收，不伪造 Windows 重测结果。
- 有失败先区分 Windows 接入问题、SDK 问题和硬件现象，保留日志；未测组合保持未完成。

每种手只维护 `docs/hardware-acceptance/<型号>/report.md`，按
[报告模板](README.md#报告模板) 追加本场日期、配置、身份、用例/断言数、退出码、
失败/跳过/未执行项及原始证据目录。保留 Linux 历史结果，未知字段写“未报告”。

换手型或 backend 后，从第 4 节重新设置并新建场次，只重复第 5 节及按需执行第 6 节。
同一型号的不同路径可整理为一张结果表。
