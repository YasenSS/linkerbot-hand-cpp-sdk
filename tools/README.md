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
