# Release artifacts

[中文](../../zh/guides/releases.md) · [Documentation index](../README.md)

## Targets aligned with transport

The Hand release workflow uses the transport v0.3.2 target matrix:

- Ubuntu 20.04, 22.04, 24.04 and 26.04, each with x86_64 and aarch64 (ARM64) Release TGZ SDKs: eight archives.
- Windows x64 and ARM64, each with Debug `/MDd` and Release `/MD` MSVC v143 ZIP SDKs: four archives.

Linux builds run in native architecture containers for each distribution, recording the image digest,
using its default GCC, libstdc++ ABI1 and C++17. Windows builds use native runners and v143 14.44.
Container checks do not certify native kernels or hardware; ARM64 SDK validation does not establish
availability of proprietary ARM64 drivers.

Hand SDK packages contain both hand and paired transport static libraries, headers, CMake packages,
documentation and read-only command-line examples. Test executables and proprietary drivers/libraries
are excluded. Versions remain independent: hand `0.3.0` can use transport `0.3.2`; transport must be
no older than hand and share its minor version.

## Names and checksums

Examples use hand version `0.3.0`; compiler versions reflect the actual build:

```text
linkerbot-hand-cpp-0.3.0-Linux-aarch64-ubuntu24.04-GNU13.3.0-libstdcxx-abi1-Release.tar.gz
linkerbot-hand-cpp-0.3.0-Windows-ARM64-msvc143-19.44.35229.0-Debug-MDd.zip
```

Each archive has an adjacent `.sha256` file. The complete set also contains `release-manifest.json`
and `SHA256SUMS.txt`: 26 files in total. The manifest records both source SHAs, build identities,
archive sizes and verified object counts. Collection requires all twelve distinct targets and checks
filenames, metadata and every static-library object's actual machine type. Missing archives, checksum
mismatches, wrong architectures/CRT settings and source identity mismatches fail collection.

Version tags or manual dispatch trigger validation and upload Actions artifacts. The workflow does
not publish a GitHub Release or replace existing assets. Refer to actual attachments on
[Releases](https://github.com/linker-bot/linkerbot-hand-cpp-sdk/releases) for download availability.
For separate prebuilt transport consumption, see the [build guide](validation.md).
