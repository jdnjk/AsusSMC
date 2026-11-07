#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Standalone AsusSMC build. Downloads pinned SDKs, builds/packages only; never installs.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
MODE="${1:-Release}"
if [[ "$MODE" == -h || "$MODE" == --help ]]; then
    printf 'Usage: bash build.sh [Debug|Release|all|test]\nArtifacts: build/dist/<configuration>/\nNo installation or EFI changes are performed.\n'
    exit 0
fi
if [[ "$MODE" == test ]]; then
    CXX="${CXX:-clang++}"
    if ! command -v "$CXX" >/dev/null 2>&1; then
        if [[ "$CXX" == clang++ ]] && command -v g++ >/dev/null 2>&1; then CXX=g++;
        else printf '%s\n' 'SKIP: no host C++ compiler.' >&2; exit 77; fi
    fi
    mkdir -p build/tests
    for TEST in control_policy control_client; do
        "$CXX" -std=c++14 -Wall -Wextra -Werror "Tests/${TEST}_test.cpp" -o "build/tests/$TEST"
        "build/tests/$TEST"
    done
    python3 -I -B -m unittest discover -s Tests -p 'test_*.py' -v
    exit 0
fi
case "$MODE" in Debug|Release|all) ;; *) printf 'Unknown configuration: %s\n' "$MODE" >&2; exit 2 ;; esac
if [[ "$(uname -s)" != Darwin ]] || ! command -v xcodebuild >/dev/null 2>&1; then
    printf '%s\n' 'SKIP: macOS with full Xcode is required; no kext was built or installed.' >&2
    exit 77
fi
SDK_VERSION="$(xcrun --sdk macosx --show-sdk-version)"
if [[ -n "${ASUSSMC_REQUIRE_SDK_MAJOR:-}" && "${SDK_VERSION%%.*}" != "$ASUSSMC_REQUIRE_SDK_MAJOR" ]]; then
    printf 'Required macOS SDK major %s, selected %s. Select the correct DEVELOPER_DIR before building.\n' "$ASUSSMC_REQUIRE_SDK_MAJOR" "$SDK_VERSION" >&2
    exit 2
fi
mkdir -p build/logs
xcodebuild -version > build/toolchain.txt
printf 'macOS SDK %s\nminimum deployment target 10.13\n' "$SDK_VERSION" >> build/toolchain.txt
python3 -I Scripts/prepare-deps.py
cp Scripts/dependencies.lock.json build/dependencies.lock.json
CONFIGURATIONS=("$MODE")
if [[ "$MODE" == all ]]; then CONFIGURATIONS=(Debug Release); fi
REVISION="$(git rev-parse --short=12 HEAD 2>/dev/null || printf 'local')"
for CONFIGURATION in "${CONFIGURATIONS[@]}"; do
    PRODUCTS="$ROOT/build/$CONFIGURATION/products"
    DIST="$ROOT/build/dist/$CONFIGURATION"
    mkdir -p "$PRODUCTS" "$DIST"
    xcodebuild -project "$ROOT/AsusSMC.xcodeproj" -target AsusSMC -configuration "$CONFIGURATION" \
        ARCHS=x86_64 ONLY_ACTIVE_ARCH=NO CODE_SIGNING_ALLOWED=NO \
        SYMROOT="$ROOT/build/$CONFIGURATION" OBJROOT="$ROOT/build/$CONFIGURATION/obj" \
        CONFIGURATION_BUILD_DIR="$PRODUCTS" \
        | tee "$ROOT/build/logs/$CONFIGURATION.log"
    BUNDLE="$PRODUCTS/AsusSMC.kext"
    /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$BUNDLE/Contents/Info.plist"
    VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$BUNDLE/Contents/Info.plist")"
    xcrun lipo -verify_arch x86_64 "$BUNDLE/Contents/MacOS/AsusSMC"
    plutil -lint "$BUNDLE/Contents/Info.plist"
    ditto -c -k --keepParent "$BUNDLE" "$DIST/AsusSMC-$VERSION-$CONFIGURATION-$REVISION.zip"
    cp LICENSE.md "$DIST/LICENSE.md"
    cp Scripts/dependencies.lock.json "$DIST/dependencies.lock.json"
    cp build/toolchain.txt "$DIST/toolchain.txt"
    printf 'source_revision=%s\nconfiguration=%s\narchitecture=x86_64\nsigned=no\nhardware_validation=not_performed\n' \
        "$REVISION" "$CONFIGURATION" > "$DIST/build-info.txt"
    python3 -I - "$DIST" <<'PY'
from pathlib import Path
import hashlib
import sys
root = Path(sys.argv[1])
files = sorted(p for p in root.iterdir() if p.is_file() and p.name != 'SHA256SUMS.txt')
(root / 'SHA256SUMS.txt').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files))
PY
    printf 'Packaged %s: %s\n' "$CONFIGURATION" "$DIST"
done
printf '%s\n' 'Build finished. No driver/app installation, firmware access or EFI changes were performed.'
