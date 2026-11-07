# Release notes

## 0.3.3

- Require transport 0.3.4 or newer in the 0.3 minor line for source builds, parent-provided targets, prebuilt selection and installed CMake consumers. Keep the version floor in one CMake definition, independent of the hand patch version. Existing same-minor compatibility and the requirement not to use a transport patch older than hand are retained.
- Apply the same floor to release validation and package collection. Add source/prebuilt/parent-target rejection tests and installed-package minimum, older-patch and newer-patch regressions using the exported package template.
- Retain existing C++ API signatures, runtime defaults, protocol behavior and scheduling. The supported legacy transport package and target aliases remain available.

Rebuild hand and downstream applications with matching transport headers and libraries. The release bundles transport v0.3.4 (`0e190032d65ed0b315337b45b3779cf4f650ed68`); do not mix the new headers with earlier objects or libraries. Binary ABI compatibility with earlier transport SDKs is not promised.

Native SDK builds, offline regressions, installed consumers, sanitizers and static analysis do not establish new physical-device acceptance. No new hardware/firmware/driver combination, physical timing or device soak is qualified. Vendor drivers are not bundled. Existing hardware records retain their original scope.

## 0.3.2

- Run the compiler-configuration compatibility matrix as a separate required CTest item, with its own process budget; retain the fast package audit and device-test limits.
- Read the current SDK version in package-compatibility validation instead of assuming 0.3.0.
- Use transport v0.3.4 (`0e190032d65ed0b315337b45b3779cf4f650ed68`) as the source and prebuilt validation baseline. The bundled SDK pairs these exact sources; standalone consumers retain the existing same-minor, minimum-version package policy.
- Give the interstage deadline-selection regression sufficient host-scheduling headroom while retaining its delayed reply beyond the angle budget. Production deadlines are unchanged.
- Update build, installation and release documentation. Device protocols, public hand API signatures, runtime defaults and scheduling are unchanged by this dependency update.

Rebuild the hand library and downstream applications with matching transport headers and libraries. Transport v0.3.4 adds public structure fields; do not mix old objects or libraries with the new headers. Binary ABI compatibility with earlier transport SDKs is not promised.

The release matrix builds and consumes native Ubuntu 20.04/22.04/24.04/26.04 x86_64/ARM64 Release packages and Windows x64/ARM64 Debug/Release packages. Verify the release manifest and checksums and select the matching platform and runtime. Vendor drivers are not bundled.

Offline tests, installed consumers, sanitizers and static analysis do not establish new physical-device acceptance. No new hand/arm hardware combination, driver/firmware combination, physical timing or long-duration device run is qualified by this release. Existing hardware records retain their original scope.

The v0.3.1 tag was created during release validation, which subsequently found a stale package-test version. No v0.3.1 Release was published; its tag is retained and this release uses a new immutable tag.
