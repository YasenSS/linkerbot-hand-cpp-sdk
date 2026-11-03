# C++ SDK Documentation

[中文](../zh/README.md) · [Documentation overview](../README.md) · [Project home](../../README.md)

## Suggested reading order

1. **Build and install**: start with the [Linux quick start](../../README.md#quick-start-linux) or the [Windows build and integration guide](guides/windows.md).
2. **Connect**: [Prepare the communication interface](connections/prepare-interface.md), then choose [Automatic connection](connections/automatic.md) or [Manual connection](connections/manual.md). For Windows adapter commands, see [Adapter integration](connections/windows-adapters.md).
3. **Use model features**: select your device below for examples covering reads, control, command results, and sensor data.
4. **Extend your application**: read the [API guide](guides/usage.md) for unified HandDevice, native Managers, asynchronous commands, shared buses, polling, closing, and recovery.
5. **Check support boundaries**: consult [Model capabilities](reference/model-capabilities.md) and [Platform and hardware validation scope](reference/compatibility.md). For discovery issues, see the [Device discovery reference](reference/discovery.md).

## Model tutorials

| Model | Tutorial and support status |
| --- | --- |
| L6 | [Classic CAN features and native Managers](tutorials/l6.md); for optional HOP CAN FD, see [Protocol details](guides/usage.md#optional-l6-can-fd-protocol) |
| O6 | [Angles, speed, acceleration, and sensors](tutorials/o6.md) |
| L20 Lite | [Ten-joint control, configuration caches, and tactile data](tutorials/l20lite.md) |
| L20 | [Sixteen-joint control and sensors](tutorials/l20.md) |
| L30 | [CAN FD, enablement, and seventeen-joint control](tutorials/l30.md) |
| O30 | [CAN FD, twenty-joint control, and dynamic sensors](tutorials/o30.md) |
| O20 | [Currently unsupported](tutorials/o20.md) |

## Guides and references

| Document | Purpose |
| --- | --- |
| [C++ quality](guides/quality.md) | Report Card scope, toolchain and documentation checks |
| [Build and validation guide](guides/validation.md) | Dependency selection, offline test targets, CI failures and result interpretation |
| [Windows guide](guides/windows.md) | Environment, building, installation, and application integration |
| [API guide](guides/usage.md) | Unified APIs, model Managers, and lifecycle |
| [Windows adapter integration](connections/windows-adapters.md) | PCAN, MiniCANFD, and SLCAN parameters, commands, and troubleshooting |
| [Model capabilities](reference/model-capabilities.md) | Feature matrices, read sources, numeric ranges, and firmware limitations |
| [Device discovery](reference/discovery.md) | Scan configuration, complete/partial identity, candidates, and diagnostics |
| [Platform and hardware validation scope](reference/compatibility.md) | Validated environments, communication paths, and untested boundaries |

## Hardware records and contributing

- [Hardware acceptance and model summaries](../hardware-acceptance/README.en.md): per-session revisions, devices, results, and unresolved issues.
- [Windows hardware acceptance](../hardware-acceptance/windows.md) (Chinese): adapter discovery, reads, and single-joint motion/restoration checks.
- [Contributing](../../CONTRIBUTING.md): issue reporting, code conventions, and validation requirements.

Tutorials provide concrete steps, guides explain common usage, and references centralize capabilities and limitations. Both languages share historical hardware reports.
When adding or moving documents, update the [Chinese counterpart](../zh/README.md), indexes, cross-links, installation, and release-package checks.
