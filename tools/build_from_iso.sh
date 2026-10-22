#!/bin/sh
# Build mgrr from your own Xbox 360 disc image of Metal Gear Rising: Revengeance.
#
#   tools/build_from_iso.sh IMAGE.iso [linux]
#
# 1. extracts the disc into x360_extracted/ (skipped if default.xex is already there)
# 2. checks default.xex: our analysis (game/*.toml) is made for exactly this executable
# 3. runs ReXGlue codegen -> game/generated/
# 4. builds the Linux binary in build/linux
# Needs: python3, cmake, ninja, clang. No game data goes into the repository.
set -e
repo=$(cd "$(dirname "$0")/.." && pwd)
iso=${1:?usage: build_from_iso.sh IMAGE.iso [linux]}
target=${2:-linux}
XEX_SHA256=907c8ebe94f085924f230d815ffa0b89c65ce60adfd00184b047276954ddf443

if [ ! -f "$repo/x360_extracted/default.xex" ]; then
  echo "== extracting $iso"
  python3 -I "$repo/tools/xdvdfs.py" "$iso" "$repo/x360_extracted" --skip '$SystemUpdate' > /dev/null
fi
sum=$(sha256sum "$repo/x360_extracted/default.xex" | cut -d' ' -f1)
if [ "$sum" != "$XEX_SHA256" ]; then
  echo "default.xex has sha256 $sum, expected $XEX_SHA256 (different region or title update?)" >&2
  exit 1
fi

sdk=$repo/sdk
# Our fixes to the SDK (rexglue-sdk-nx), kept as patches until they are upstream.
for p in "$repo"/tools/sdk_patches/*.patch; do
  if git -C "$sdk" apply --check "$p" 2>/dev/null; then
    echo "== patching sdk: $(basename "$p")"
    git -C "$sdk" apply "$p"
  elif ! git -C "$sdk" apply --reverse --check "$p" 2>/dev/null; then
    # Neither way: applied, then partly changed by a later patch (0009 rewrites lines 0002 translated).
    echo "== sdk: $(basename "$p") taken as applied (a later patch changed some of its lines)"
  fi
done
rexglue=$sdk/out/linux-amd64/Release/rexglue
if [ ! -x "$rexglue" ]; then
  echo "== building the ReXGlue host tools"
  (cd "$sdk" && cmake --preset linux-amd64 -B "$repo/build/sdk-host" -DREXGLUE_ENABLE_TRACY=OFF)
  cmake --build "$repo/build/sdk-host" --config Release --target rexglue
fi

echo "== codegen"
(cd "$repo/game" && "$rexglue" codegen mgrr_manifest.toml)

case $target in
  linux)
    echo "== building build/linux"
    cmake -S "$repo/game" -B "$repo/build/linux" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DREXSDK_DIR="$sdk" \
          -DCMAKE_C_FLAGS=-march=x86-64-v3 -DCMAKE_CXX_FLAGS=-march=x86-64-v3   # the SDK needs SSSE3+ (its presets use v2/v3)
    cmake --build "$repo/build/linux" -j"${JOBS:-4}"
    ;;
  *) echo "unknown target $target" >&2; exit 1 ;;
esac
