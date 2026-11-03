# Build and validation guide

[中文](../../zh/guides/validation.md) · [Documentation index](../README.md)

## Select the dependency deliberately

Use the full transport commit pinned in the project README and `.github/workflows/ci.yml`.
An explicit `LINKERBOT_COMM_SOURCE_DIR` selects a source checkout. Set it to an empty
string to use an installed `linkerbot-comm` package through `CMAKE_PREFIX_PATH`.
Without an explicit value, CMake may select an adjacent `linkerbot-transport-cpp`
directory. Record the actual commit and build configuration in test reports.

## Run offline tests

From the SDK source root, with Catch2 3 installed and the pinned transport checkout
in the adjacent directory:

```bash
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release \
  -DLINKERBOT_COMM_SOURCE_DIR=../linkerbot-transport-cpp \
  -DLINKERBOT_BUILD_TESTS=ON -DLINKERBOT_BUILD_EXAMPLES=OFF \
  -DLINKERBOT_BUILD_TOOLS=OFF -DLINKERBOT_COMM_BUILD_TESTS=OFF \
  -DLINKERBOT_ALLOW_DEPENDENCY_DOWNLOADS=OFF
cmake --build build-tests --parallel 4 --target tests protocol-tests
ctest --test-dir build-tests --output-on-failure
```

Test executables are excluded from the default build: explicitly build the targets
before running CTest. A successful library build alone is not a test result.
Offline fixtures cover protocol behavior, scheduling and lifecycle contracts;
they do not certify USB drivers, firmware timing, motion accuracy or real hardware.
See [hardware validation scope](../reference/compatibility.md).

For Windows, use the [Windows guide](windows.md), build both test targets with
`--config Release`, and pass `-C Release` to CTest. Do not mix Debug and Release
libraries or runtime settings.

## Interpret command results

A returned setter handle means local admission. Check its terminal stage and error
for logical completion; an ACK is not proof of mechanical arrival. For physical
batch evidence, use `send_results()` as described in the
[API guide](usage.md#inspect-physical-command-delivery). A timeout or lifecycle
failure does not prove that a claimed write sent nothing. Do not automatically
replay movement or persistent configuration after an uncertain result.

## Investigate CI failures

Start with the failed step and its first concrete error. Record the SDK and
transport commits, platform, compiler, build configuration, test name and random
seed. Windows artifacts contain build logs, test results and package evidence.
Distinguish compilation failures, protocol assertions, explicit deadline tests and
functional tests affected by host scheduling. Keep deadline tests strict; give
encoding-only tests an explicit budget without changing production defaults.

PR jobs run offline tests and protocol checks. Main-branch jobs additionally run
installation, public-header, documented-example and package checks according to
the workflow's platform conditions. A skipped check is not a pass. Historical
hardware reports apply only to their recorded revision and environment.

## Repository-local editor settings

`.vscode/` is ignored in full. Keep machine paths and personal launch settings
local; maintained build and test commands belong in CMake, the justfile and docs.
