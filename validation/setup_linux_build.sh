#!/usr/bin/env bash
# Run inside the persistent native release container, before restoring caches.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential git ca-certificates python3 python3-venv ninja-build ccache
python3 -m venv /opt/release-tools
/opt/release-tools/bin/pip install cmake==3.31.10
test "$(uname -m)" = "$EXPECTED_ARCH"
git config --global --add safe.directory /src/hand-cpp
git config --global --add safe.directory /src/linkerbot-transport-cpp

# Include the image digest in the Actions key, and fingerprint the packages
# installed on top of that image as well as the actual compiler binary.
{
    dpkg-query -W -f='${Package}=${Version}\n' | LC_ALL=C sort
    sha256sum "$(readlink -f /usr/bin/g++)"
    /opt/release-tools/bin/cmake --version
    ninja --version
    ccache --version
} | sha256sum | cut -d ' ' -f 1 > /release/toolchain-id
