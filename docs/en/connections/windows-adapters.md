# Windows Adapter Integration

This guide explains how to read devices through PCAN, MiniCANFD, or SLCAN, then use the same connection in your own C++ program.
For tested configurations by model, see [Platform and hardware validation scope](../reference/compatibility.md#platform-and-hardware-validation-scope).
L6 defaults to Classic CAN, which is used by the L6 examples below. For CAN FD devices, explicitly select HOP as described in
[Optional protocol](../guides/usage.md#optional-l6-can-fd-protocol) and use a matching FD adapter.

## 1. Prepare the environment

Build `windows_readonly.exe` using the [Windows build instructions](../guides/windows.md#building-the-sdk).
Install vendor drivers and DLLs separately; both DLLs and applications must be x64.

| Adapter | Requirements | Example channel |
| --- | --- | --- |
| PCAN | PEAK driver and `PCANBasic.dll`; FD devices require an FD-capable adapter | `PCAN_USBBUS1` |
| MiniCANFD | Vendor driver, `HCanbus.dll`, and its dependencies | `0:0`, meaning device index:channel index |
| SLCAN | Serial driver; confirm whether firmware uses Lawicel Classic or CANable FD | `COM10` |

Set the program path in PowerShell at the source root:

```powershell
$Read = '.\build-windows\examples\Release\windows_readonly.exe'
& $Read --help
```

For a Debug build, replace `Release` with `Debug` in the path. Adjust the DLL paths, channels, models,
hand sides, addresses, and bitrates below for the actual devices. The command exit code `$LASTEXITCODE` should be `0`.

## 2. Select an adapter and read

For each adapter, enumerate first, then read. `--read` discovers devices, reads identity and angles, then closes the connection
without sending motion targets. Use `--discover` for discovery only, or use
`--manual` to skip discovery when the model, hand side, and address are known. Select only one action per invocation.

### PCAN Classic

For L6, O6, L20 Lite, and L20 with Classic firmware. Enumerate first, then replace the channel with an actual result.

```powershell
$Pcan = 'C:\vendor\PCANBasic.dll'
& $Read --list --backend pcan --library $Pcan

$Can = @('--backend','pcan','--channel','PCAN_USBBUS1','--mode','classic',
  '--library',$Pcan,'--bitrate','1000000')
& $Read --read @Can --model l6 --side left --address 0x28
```

### MiniCANFD

For L30 and O30. Use the device and channel indices from enumeration; on Windows, the channel is fixed at `0`.
Successful DLL enumeration does not guarantee transmission or reception. If sending fails, verify that the DLL matches the device.

```powershell
$Mini = 'C:\vendor\HCanbus.dll'
& $Read --list --backend minicanfd --library $Mini

$Can = @('--backend','minicanfd','--channel','0:0','--mode','fd',
  '--library',$Mini,'--bitrate','1000000','--data-bitrate','5000000')
& $Read --read @Can --model l30 --side right --address 1
```

### SLCAN / CANable FD

This example connects to L30 with CANable FD firmware. Use the COM port from Device Manager; only the specified port is accessed, and no vendor DLL is needed.

```powershell
$Can = @('--backend','slcan','--channel','COM10','--mode','fd',
  '--slcan-protocol','canable-fd','--serial-baud','115200',
  '--bitrate','1000000','--data-bitrate','5000000','--serial-timeout-ms','500')
& $Read --list @Can
& $Read --read @Can --model l30 --side right --address 1
```

| Device and firmware | Parameter changes |
| --- | --- |
| O30 + CANable FD | Use `--model o30` and the actual hand side and HOP request ID |
| L6/O6/L20 Lite/L20 + CANable FD | Keep `--slcan-protocol canable-fd`, use `--mode classic`, omit `--data-bitrate`, and adjust model, side, and address |
| Classic model + Lawicel firmware | Use `--slcan-protocol lawicel-classic --mode classic`, without `--data-bitrate` |

`--serial-baud` is the serial baud rate, while `--bitrate` is the CAN bitrate; match them to the firmware and device configuration.
Lawicel Classic does not support FD; CANable FD supports data bitrates of 2M or 5M.
If the serial device needs time to stabilize after opening, add `--serial-startup-ms 100`.

### PCAN FD

For FD-capable PCAN adapters connected to L30 or O30; hardware validation is still pending.
Confirm that enumeration reports `FD=yes`, then set bit timing matching the device. The following uses an 80 MHz clock and 1M/5M bitrates:

```powershell
$Pcan = 'C:\vendor\PCANBasic.dll'
& $Read --list --backend pcan --library $Pcan

$Can = @('--backend','pcan','--channel','PCAN_USBBUS2','--mode','fd',
  '--library',$Pcan,'--bitrate','1000000','--data-bitrate','5000000',
  '--fd-clock','80000000','--nom-brp','1','--nom-tseg1','63','--nom-tseg2','16','--nom-sjw','16',
  '--data-brp','1','--data-tseg1','11','--data-tseg2','4','--data-sjw','4')
& $Read --read @Can --model l30 --side right --address 1
```

L30 uses extended FD and a NodeID as `--address`; O30 uses standard FD without BRS and a HOP request ID as `--address`.
The SDK selects the frame format by model; configure the channel name and device address separately.

## 3. Connect in a C++ program

On Windows, pass an adapter factory or shared Session to `HandDevice`.
The following is a complete PCAN Classic example for a left-hand L6:

```cpp
#include <linkerbot/hand/adapter_transport.hpp>
using namespace linkerbot;

int main() {
    can::PcanConfig pcan;
    pcan.channel = can::pcan_channel_handle("PCAN_USBBUS1");
    pcan.library_path = "C:/vendor/PCANBasic.dll";
    pcan.nominal_bitrate = 1'000'000;

    hand::HandDeviceConfig config;
    config.model = hand::Model::kL6;
    config.side = hand::HandSide::kLeft;
    config.interface_name = "pcan:PCAN_USBBUS1";
    config.address = 0x28;

    hand::HandDevice device(config, hand::make_pcan_transport_factory(pcan));
    device.open();
    const auto info = device.get_device_info();
    const auto angles = device.get_angles();
    device.close();
}
```

To change adapters, replace the configuration and factory, and update the model, side, address, and `interface_name`:

| Adapter | Configuration type | Factory creation |
| --- | --- | --- |
| PCAN | `can::PcanConfig` | `hand::make_pcan_transport_factory(pcan)` |
| MiniCANFD | `can::VendorCanFdTransport::Config` | `hand::make_minicanfd_transport_factory(mini)` |
| SLCAN | `can::SlcanConfig` | `hand::make_slcan_transport_factory(serial)` |

For MiniCANFD, set `device_index`, `channel_index`, `library_path`, `nom_baud`, and `dat_baud`.
For SLCAN, set `port`, `serial_baud`, and `nominal_bitrate`. CANable FD firmware also requires
`protocol = can::SlcanProtocol::kCanableFd`; select `fd` and `data_bitrate` for the device.
PCAN FD requires `mode = can::PcanMode::kFd`, both bitrates, and `fd_timing`.

`open()` establishes the connection and verifies identity. After a successful read, control and polling use the same
[HandDevice API](../guides/usage.md#reading-and-control).
You can also create a device with bound adapter configuration through [Device discovery](../reference/discovery.md).

## Angle-mapping configuration directory

Set a file path through `config.angle_mapping_path`. If omitted, the SDK checks
`XDG_CONFIG_HOME`, `LOCALAPPDATA`, and `APPDATA` in order, then `.config` under `HOME` or `USERPROFILE`.
The default file is `linkerbot/hand_angle_mappings.toml` under the selected directory. Paths containing Chinese characters are supported.

## Troubleshooting

| Symptom | Check first |
| --- | --- |
| DLL loading fails | DLL architecture (x64), correct path, and required dependencies |
| Enumeration succeeds but reads fail | Power, wiring, termination, CAN bitrate, device address, and driver/DLL versions |
| Channel or serial port is busy | Close other control programs using the same channel |
| SLCAN reports `allowlist_required` | Explicitly specify the actual COM port; with discovery, set `discovery.slcan.channels` |
| SLCAN FD configuration is rejected | CANable FD-compatible firmware and a transport library with FD extension support |
| Windows cannot connect through `can0` | Create the device through a Windows adapter factory or discovery API |
