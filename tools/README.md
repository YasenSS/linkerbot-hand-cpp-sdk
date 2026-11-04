# Developer tools

[English](README.md) | [简体中文](README.zh-CN.md)

## Read L6 right-hand device information

Configure with `LINKERBOT_BUILD_TOOLS=ON`, then run:

```bash
cmake --build build --target l6_read_version
./build/tools/l6_read_version can0
```

This reads the serial number, firmware/PCB/mechanical versions and one angle frame.
The default interface is `can0`, using the L6 right-hand protocol. It does not send
motion targets or configuration writes. `--help` is available without hardware.

Exit status: `1` for invalid arguments, `2` for version-query failure, `3` for
angle-query failure. Prepare the bus using the [connection guide](../docs/en/connections/prepare-interface.md).

## Frequency benchmark entry points

These targets are Linux-only and excluded from the default build. Configure with
`LINKERBOT_BUILD_TOOLS=ON`, then build explicitly:

```bash
cmake --build build --target l6_frequency_benchmark o6_frequency_benchmark --parallel 4
./build/tools/l6_frequency_benchmark --help
./build/tools/o6_frequency_benchmark --help
```

`--help` performs no device I/O. L6 offers `fake`, `fake-socketcan`, `socketcan` and
`sdk` layers; O6 offers `fake`, `socketcan` and `sdk`. Read each tool's options before
selecting a hardware layer: benchmark modes may send control traffic. Choose `fake`
for an in-process simulated transport. `l6_raw_session.cpp` is shared support code,
not a separate command-line program.

Record the exact command, layer, source revisions, build configuration, interface,
bitrate, duration and observed errors with benchmark results. Host scheduling,
firmware and adapters affect measured throughput; a simulated rate is not a hardware
control-frequency guarantee. On Windows use the maintained read-only examples and
[adapter guide](../docs/en/connections/windows-adapters.md), not these Linux tools.
