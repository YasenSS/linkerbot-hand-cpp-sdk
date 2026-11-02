# Platform and hardware validation scope

The unreleased 0.3.0 API has removed O30 motion-time and torque-limit read/write APIs, model entry points, samples, events, and snapshot fields.
Remove old calls and rebuild; actual torque reads and current-limit reads/writes remain available, with surviving public enum values unchanged.
Historical hardware acceptance refers to the SDK revision recorded in each report, not the API inventory after these removals.

L6 position Kp, position Kd, and torque Kd are available only through native `l6::L6` Managers.
The corresponding unified read/write methods, samples, snapshot fields, events, and capabilities have been removed. Migrate to the Managers'
`set_values()`, `get_blocking()`, and `get_snapshot()` and rebuild. Native protocol and raw-value semantics remain unchanged.

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
