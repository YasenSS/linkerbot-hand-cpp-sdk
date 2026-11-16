# Release artifacts

[中文](../../zh/guides/releases.md) · [Documentation index](../README.md)

## 0.3.12 scheduling update

Version 0.3.12 adds angle scheduling at safe tactile-stage boundaries under the
explicit Python runtime policy while retaining Native defaults and existing C++ APIs.
The [release notes](../../../RELEASE_NOTES.md#0312) describe compatibility, recorded
validation, O6/L20/O30 hardware results and the untested shared-Session/fault timing scope.

## Current workflow targets

The current Hand release workflow builds the following targets. Previously published assets remain unchanged:

- Ubuntu 20.04, 22.04, 24.04 and 26.04, each with x86_64 and aarch64 (ARM64) Release TGZ SDKs: eight archives.
- Windows x64 and ARM64, each with Release `/MD` MSVC v143 ZIP SDKs: two archives.

Linux builds run in native architecture containers for each distribution, recording the image digest,
using its default GCC, libstdc++ ABI1 and C++17. Windows builds use native runners and v143 14.44.
Container checks do not certify native kernels or hardware; ARM64 SDK validation does not establish
availability of proprietary ARM64 drivers.

Windows CI uses Ninja with the native MSVC v143 14.44 environment to compile up to
four source files concurrently. CI opts into sccache. Debug ZIP packages are no longer
built by the release workflow; local Debug builds remain supported.
GitHub Actions caches compiler outputs and a shared, pinned Catch2 3.8.1 installation;
transport and hand tests use the same installation. Caches are separated by target
architecture, configuration and compiler/Windows SDK identity. Catch2 additionally
keys on CMake and preparation scripts. Cold caches build normally; cache reuse does
not skip tests, package audits or source/architecture checks. Diagnostic artifacts
include compiler cache statistics and per-step validation durations. No additional
repository secrets or external cache service are required.

Linux CI also uses Ninja, with ccache supplied by each target distribution.
The native container stays alive across preparation, cache restoration and validation.
Compiler outputs and the shared C++17 PIC Catch2 installation are cached separately,
partitioned by distribution, architecture, image digest and installed toolchain identity.
The default distribution GCC and libstdc++ ABI1 remain unchanged. All tests, package audits,
relocation checks and header/example compilation still run. Evidence includes ccache statistics.

Hand SDK packages contain both hand and paired transport static libraries, headers, CMake packages,
documentation and read-only command-line examples. Test executables and proprietary drivers/libraries
are excluded. Versions remain independent. Hand 0.3.12 requires transport 0.3.4 or newer in
the 0.3 minor line in source, prebuilt and installed-consumer builds.


The paired source pin and prebuilt downloads use the same v0.3.4 release.
Rebuild hand and downstream consumers with matching transport headers and libraries;
public transport structures have grown. Dependency pairing itself adds no hardware
acceptance; the 0.3.12 scheduling evidence has the limited scope linked above.
Transport archives now use the `linkerbot-transport-` prefix.
Hand continues to consume the supported `linkerbot-comm` CMake package and CAN target aliases.

## Verify cache benefits

Re-run a completed Release job on the same source and PR to compare a cold cache
with a restored cache. Check the cache-restoration logs, Catch2 preparation time,
compiler hit/miss counts and per-step durations in the diagnostic artifacts.
Keep the full tests and package checks enabled. Compare job execution time
separately from runner queue time; a new PR may have a different cache scope.
Changes to the toolchain or cache preparation scripts can intentionally cause a cold run.

For source commit `c7ba44a`, [Release packages run 43827841694](https://github.com/YasenSS/linkerbot-hand-cpp-sdk/actions/runs/43827841694)
recorded the following native Windows Release comparisons:

| Architecture | Cold job | Warm job | Compiler cache hits | Warm cache errors |
| --- | --- | --- | --- | --- |
| x64 | 19 min 34 s | 5 min 49 s | 771 / 793 (97.23%) | 0 |
| ARM64 | 18 min 50 s | 7 min 29 s | 771 / 793 (97.23%) | 0 |

Both warm jobs reused the installed Catch2 and passed the complete acceptance.
These are same-source, same-PR measurements on hosted runners, not guaranteed timings
for changed sources or other runner/toolchain versions. Linux workflow timings must be
measured independently; the Windows comparison does not establish Linux speedups.

## Names and checksums

Examples use hand version `0.3.12`; compiler versions reflect the actual build:

```text
linkerbot-hand-cpp-0.3.12-Linux-aarch64-ubuntu24.04-GNU13.3.0-libstdcxx-abi1-Release.tar.gz
linkerbot-hand-cpp-0.3.12-Windows-ARM64-msvc143-19.44.35229.0-Release-MD.zip
```

Each archive has an adjacent `.sha256` file. The complete set also contains `release-manifest.json`
and `SHA256SUMS.txt`: 22 files in total. The manifest records both source SHAs, build identities,
archive sizes and verified object counts. Collection requires all ten distinct targets and checks
filenames, metadata and every static-library object's actual machine type. Missing archives, checksum
mismatches, wrong architectures/CRT settings and source identity mismatches fail collection.

Changes to build/release files in pull requests, version tags or manual dispatch trigger validation
and upload Actions artifacts. PR artifacts describe the tested merge commit.

To attach the complete set to a GitHub Release, manually run **Release packages** on an existing
matching `v<version>` tag, supply the full transport SHA and version, and enable `create_draft`.
The exact tagged hand commit must already have a successful main-push or manually dispatched
**C++ Report Card** run under the A+ gate. All ten builds and collection must pass first.
The optional job creates a new **draft** with all 22 files and downloads them to verify their bytes.
It rejects branch dispatches, moved/mismatched tags and existing releases; it never replaces assets.
Default runs only upload Actions artifacts. Publishing the draft remains a separate maintainer action.
Refer to actual attachments on [Releases](https://github.com/linker-bot/linkerbot-hand-cpp-sdk/releases)
for download availability. For separate prebuilt transport consumption, see the [build guide](validation.md).
