# Platform and hardware validation scope

[中文](../../zh/reference/compatibility.md) · [Documentation index](../README.md)

## 0.3.12 compatibility and scheduling validation

Existing public C++ APIs and Native defaults remain available. The tactile-stage
scheduler requires explicit `RuntimeBehavior::kPython`; the
[Python behavior baseline](../guides/python-behavior-baseline.md#tactile-stage-angle-scheduling-0312)
records supported protocols and scheduling rules. Hand 0.3.12 requires transport
0.3.4 or newer in the 0.3 minor line. Rebuild consumers with matching headers and libraries.

L6 position Kp/Kd and torque Kd are available through both the unified API and
native Managers. O30 unified torque-limit and motion-time methods require explicit
`O30Layout::kPythonV25`; native Managers retain the mounted V36 layout. See the
[API guide](../guides/usage.md#features-unified-through-handdevice) for entry points. Earlier 0.3.0
API-removal notes do not describe the current interface inventory.

O6 left, L20 right and O30 right (explicit V25) passed query-only,
command-feedback-only, adaptive, five-minute saturation and Native-control scheduling
groups on Ubuntu 24.04 x86_64 / SocketCAN / Release. Firmware, adapters, tested
revisions and per-device results are recorded in the
[0.3.12 release notes](../../../RELEASE_NOTES.md#0312).
No new L6/L20Lite scheduling hardware acceptance or physical shared-Session,
close/recovery and injected abnormal-reply timing acceptance is claimed. Offline
regressions cover these lifecycle and isolation rules independently. The scheduling
runs do not establish frequency maxima or dynamic motion accuracy.

## Historical common-feature hardware baseline

Historical hardware acceptance applies to the SDK revision and configuration recorded
in each report. It does not automatically validate changes in later releases.

The `0.3.0` validation baseline is Ubuntu 24.04 x86_64 / GCC 13 / C++17 and Windows 11 x64 /
Visual Studio 2022 / MSVC v143 14.44 / C++17. Other operating systems, architectures, toolchains, and driver versions
are outside this baseline. Windows Release uses `/MD`; Debug uses `/MDd`. Match the toolchain and configuration
of prebuilt packages and rebuild applications when updating the SDK.

L6 hardware results below cover default Classic CAN. The optional HOP CAN FD protocol has only offline protocol-vector and
FakeTransport validation; hardware acceptance is pending.

| Communication path | Hardware validation scope |
| --- | --- |
| Linux SocketCAN | Common SDK functions passed on L6, O6, L20 Lite, L20, L30, and O30 |
| Windows PCAN Classic | Debug adapter checks passed on L6, O6, L20 Lite, and L20 |
| Windows PCAN FD | Backend and offline tests implemented; L30/O30 hardware untested |
| Windows MiniCANFD | O30/L30 hardware adapter checks passed; O30 used Debug and x64 `HCanbus.dll 3.3.103.0`; L30 build configuration and DLL version were not recorded |
| Windows SLCAN | Debug adapter checks passed for all six models, covering only the recorded CANable profiles; integration remains experimental |
| Native Linux PCAN, MiniCANFD, SLCAN | No hardware pass claim in this validation round; serial PTYs and dynamic-library fixtures are not real hardware |

Windows adapter checks cover discovery, opening, identity/angle reads, closing, and one single-joint command with restoration.
Release engineering, installation, and ZIP consumption passed on 2026-10-19 and are recorded separately from Debug hardware results.
These results do not cover Release hardware, Windows USB hotplug, long-term load, or all-joint Windows motion;
they do not guarantee mechanical positioning accuracy. Factory reset, EEPROM persistence, and advanced tuning are outside common-feature
hardware acceptance. CANable-profile results do not cover Lawicel firmware for SLCAN. Obtain vendor drivers and DLLs
separately. For detailed devices, firmware, and limits, see `docs/hardware-acceptance/README.md` in the source repository.
