#!/bin/sh
# Compile the generated game code (game/generated/default) for Switch with the host clang instead of devkitA64 GCC
# (S0c: clang code ran 1.4x faster per function). Objects go to build/switch-clang/; the rest of the NRO stays GCC.
#
#   tools/switch_build/clang_recomp.sh [JOBS]
#
# Needs build/dka64-sysroot (headers copied from the devkitpro/devkita64 container: newlib+libstdc++, libnx, portlibs)
# and a configured build/switch (for the SDK's generated include dir).
set -e
repo=$(cd "$(dirname "$0")/../.." && pwd)
jobs=${1:-5}
sys=$repo/build/dka64-sysroot
gen=$repo/game/generated/default
out=$repo/build/switch-clang
mkdir -p "$out"
[ -d "$sys/newlib/c++/15.2.0" ] || { echo "no $sys (see NOTES.md, M1 clang)"; exit 2; }

FLAGS="--target=aarch64-none-elf -mcpu=cortex-a57 -fPIE -ffunction-sections -fdata-sections
 -O${OPT:-2} -DNDEBUG -std=gnu++23 -fno-strict-aliasing -ffp-contract=off -fno-math-errno -w
 -D__SWITCH__ -DNX -D_GNU_SOURCE -DSPDLOG_NO_TZ_OFFSET -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -DREX_PLATFORM_SWITCH=1
 -nostdinc++ -isystem $sys/newlib/c++/15.2.0 -isystem $sys/newlib/c++/15.2.0/aarch64-none-elf -isystem $sys/newlib
 -isystem $sys/dka -isystem $sys/libnx -isystem $sys/portlibs -isystem $repo/sdk/switch_compat
 -I$repo/game -I$repo/game/src -I$gen -I$repo/sdk/include -I$repo/build/switch/rexglue-sdk/include
 -I$repo/sdk/thirdparty/simde -I$repo/sdk/thirdparty/simde/simde -I$repo/sdk/thirdparty/fmt/include
 -I$repo/sdk/thirdparty/spdlog/include -I$repo/sdk/thirdparty/xxHash -isystem $repo/sdk/thirdparty/tomlplusplus/include"

# mgrr_register.cpp is left to GCC (one huge function run once at start-up; clang -O1 did not finish it in 15 min)
ls "$gen"/mgrr_init.cpp "$gen"/mgrr_recomp.*.cpp | \
  xargs -P "$jobs" -I{} sh -c '
    f={}; o='"$out"'/$(basename "$f" .cpp).o
    [ -f "$o" ] && [ "$o" -nt "$f" ] && exit 0
    clang++ '"$(echo $FLAGS)"' -c "$f" -o "$o" || { echo "FAILED $f"; exit 1; }'
echo "objects: $(ls "$out"/*.o | wc -l) in $out"
[ "$NOLINK" = 1 ] && exit 0
# Link: the GCC build's own link command (build/switch, ninja) with the recomp objects swapped for these.
cd "$repo"
docker run --rm -v "$repo:$repo" -w "$repo" devkitpro/devkita64 sh -c "ninja -C build/switch -t commands mgrr | tail -1" > build/switch_link_cmd.txt
python3 - "$repo" <<'PY'
import re, sys
repo = sys.argv[1]
c = open(repo + '/build/switch_link_cmd.txt').read().strip().split('&&', 1)[1].strip()
def sub(m):
    return m.group(0) if m.group(1) == 'mgrr_register' else '%s/build/switch-clang/%s.o' % (repo, m.group(1))
c, n = re.subn(r'CMakeFiles/mgrr_recomp\.dir/generated/default/(mgrr_[a-z_]+(?:\.\d+)?)\.cpp\.obj', sub, c)
c = re.sub(r'-o mgrr(\s)', r'-o mgrr_clang\1', c)
open(repo + '/build/switch_link_clang.sh', 'w').write('cd %s/build/switch && %s\n' % (repo, c))
print('objects swapped:', n - 1)
PY
docker run --rm -u "$(id -u):$(id -g)" -v "$repo:$repo" -w "$repo" devkitpro/devkita64 sh -c "sh build/switch_link_clang.sh && cd build/switch &&
  P=/opt/devkitpro; cp mgrr_clang mgrr_clang.stripped.elf && \$P/devkitA64/bin/aarch64-none-elf-strip --strip-all mgrr_clang.stripped.elf &&
  \$P/tools/bin/nacptool --create 'Metal Gear Rising (clang)' mgr-nx 0.0.1 mgrr_clang.nacp >/dev/null &&
  \$P/tools/bin/elf2nro mgrr_clang.stripped.elf mgrr_clang.nro --nacp=mgrr_clang.nacp --icon=\$P/libnx/default_icon.jpg"
echo "built build/switch/mgrr_clang.nro"
