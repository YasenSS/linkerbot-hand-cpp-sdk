# Contributing

[English](CONTRIBUTING.md) | [简体中文](CONTRIBUTING.zh-CN.md)

Read the [project guide](README.md), [validation guide](docs/en/guides/validation.md),
[compatibility policy](docs/en/reference/compatibility.md) and existing issues before changing behavior.

## Report a problem

Include the hand SDK and transport commits, local modifications, OS, architecture,
compiler, CMake version, build configuration and Windows runtime selection.
Provide a minimal reproducer, exact commands, expected/actual behavior and the complete error.
For hardware issues, also record model, side, firmware, adapter/backend, driver/DLL,
channel, bitrate and whether another process controls the device.

Remove credentials, private server addresses and personal directory information from logs.
Do not upload vendor drivers, DLLs or non-public hardware captures.

## C++ implementation rules

The supported language level is C++17; do not require newer language features without
an explicit compatibility change. Follow C++ Core Guidelines principles where they fit
this SDK: RAII ownership, explicit lifetime contracts, checked boundaries and narrow interfaces.

- Keep protocols, device runtime, transport and public APIs in their existing layers.
- Validate sizes, ranges and capabilities before physical I/O. Preserve typed errors and context.
- Use monotonic deadlines for elapsed time. Preserve cancellation, partial-send and late-result semantics.
- Do not hold a physical writer lock across ACK waits or execute user callbacks on the bus receive path.
- Preserve public enum values and source compatibility unless a documented migration is intentional.
- Use leading return types for functions. Let lambdas infer their result where possible.
- Use braces for control-flow bodies, including single statements. Keep `else if` chains.
- Follow `.clang-format` with clang-format 18 (minimum 15 for `InsertBraces`), 120 columns,
  four-space indentation and opening braces on the current line.

Run `just format-check` with the configured formatter available. The
[quality guide](docs/en/guides/quality.md) describes the Report Card scope and current policy.
Static analysis findings require review; a score does not replace tests or hardware evidence.

## Validate a change

Keep patches focused. Explain the trigger, resulting behavior, compatibility impact and evidence.
Add regression coverage for observable defects, including timeout and lifecycle boundaries where relevant.
Use synchronization barriers for concurrency tests; do not rely on host scheduling to prove an ordering.
Encoding-only tests can have an explicit host budget; deadline-contract tests must still test the exact boundary.

Run the relevant offline tests. Build or installation changes also require an installed
`find_package` consumer. Hardware tests require the operator to prepare the actual environment;
record skipped or unperformed checks explicitly rather than calling them passes.
See [hardware test instructions](tests/hand/device/hardware/README.md).

## Maintain documentation

English is the default entry point. Keep Chinese counterparts and reciprocal language links current.
Use exact API names, units, firmware limits and runnable commands; separate receipt, ACK and mechanical outcomes.
Do not present historical hardware reports as validation of a newer commit.
The `.en.md` landing pages are compatibility aliases; the documentation check keeps them aligned.
Do not commit machine-specific `.vscode` files or generated build artifacts.

Contributions are distributed under the [MIT license](LICENSE). Only submit material you are entitled to publish.
