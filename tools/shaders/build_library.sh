#!/bin/sh
# Build MGR's shader library (build/shaders/mgrr_shaders.mgsl) from the extracted disc (x360_extracted/).
#
#   DXC=/path/to/dxc tools/shaders/build_library.sh [JOBS]
#
# 1. data000.cpk -> shader.dat, shader2.dat, shadereff.dat -> containers (tools/cpk.py, tools/pgdat.py)
# 2. containers -> HLSL (mgrr_hlsl, vendored XenosRecomp with xbox360-nx's changes)
# 3. HLSL -> SPIR-V with DXC, in the deko3d mode the SDK renderer expects (flags from xbox360-nx's NFS pipeline)
# 4. pack, and load it back the way the game does (mgrr_pack_shaders); --xsh checks identification against the
#    microcode a PC run loaded, when that file exists
# 5. every SPIR-V through SPIR-V -> GLSL -> DKSH, the console's compile path (dksh_probe)
# The library is derived from the game: it stays in build/ and on the player's SD card, never in git.
set -e
repo=$(cd "$(dirname "$0")/../.." && pwd)
jobs=${1:-8}
out=$repo/build/shaders
DXC=${DXC:?set DXC to a DXC binary (Vulkan SDK, or directx-shader-compiler)}
[ -f "$repo/x360_extracted/media/data000.cpk" ] || { echo "no x360_extracted/media/data000.cpk"; exit 2; }

echo "== tools"
cmake -S "$repo/tools/shaders/library" -B "$repo/build/shader_tools" -G Ninja -DCMAKE_CXX_COMPILER=clang++ >/dev/null
cmake --build "$repo/build/shader_tools" >/dev/null
cmake -S "$repo/tools/shaders/dksh_probe" -B "$repo/build/dksh_probe" -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang >/dev/null
cmake --build "$repo/build/dksh_probe" >/dev/null

rm -rf "$out"
mkdir -p "$out/dat" "$out/containers" "$out/hlsl" "$out/spirv"
echo "== containers"
python3 -I "$repo/tools/cpk.py" x "$repo/x360_extracted/media/data000.cpk" "$out/dat" '(^|/)shader(2|eff)?\.dat$' >/dev/null
for dat in shader shader2 shadereff; do
  python3 -I "$repo/tools/pgdat.py" x "$out/dat/$dat.dat" "$out/containers/$dat" >/dev/null
done
ls "$out"/containers/*/*.?so | wc -l

echo "== HLSL"
"$repo/build/shader_tools/mgrr_hlsl" "$out/hlsl" "$repo/tools/shaders/xenosrecomp/shader_common.h" \
  "$out"/containers/*/*.?so > "$out/hlsl.log" || true
tail -1 "$out/hlsl.log"

echo "== SPIR-V"
ls "$out"/hlsl/*.hlsl | xargs -P "$jobs" -I{} sh -c '
  f={}; b=$(basename "$f" .hlsl)
  case $b in p_*) t=ps_6_6;; *) t=vs_6_6;; esac
  "'"$DXC"'" -spirv -T $t -E main -HV 2021 -fspv-target-env=vulkan1.2 -fvk-use-dx-layout \
    -Werror=parameter-usage -DNFSMW_DEKO3D -Fo "'"$out"'/spirv/$b.spv" "$f" > "'"$out"'/spirv/$b.log" 2>&1 \
    || echo "DXC failed: $b"' > "$out/dxc.log"
echo "$(ls "$out"/spirv/*.spv | wc -l) compiled, $(wc -l < "$out/dxc.log") failed (dxc.log)"

echo "== pack"
xsh=$HOME/.local/share/mgrr/cache/shaders/shareable/4B4E080A.xsh
set -- "$out"/containers/*/*.?so
[ -f "$xsh" ] && set -- --xsh "$xsh" "$@"
"$repo/build/shader_tools/mgrr_pack_shaders" "$out/mgrr_shaders.mgsl" "$out/spirv" "$@" 2>&1 \
  | grep -v "^no SPIR-V" | tee "$out/pack.log"

echo "== DKSH (console compile path)"
"$repo/build/dksh_probe/dksh_probe" "$out/dksh.tsv" "$out"/spirv/*.spv
