#!/bin/sh
# Build the S0 benchmark from generated variant dirs (GEN/<tag>/chunk_*.cpp + table.cpp).
#   build.sh pc          GEN OUTDIR [count]  -> OUTDIR/s0_bench or s0_count (host compilers)
#   build.sh arm         GEN OUTDIR [count]  -> same, inside an arm64 Linux container with g++ and clang (qemu)
#   build.sh switchclang GEN OUTDIR          -> on the host: clang objects of the c* variants for the Switch
#                                              (needs DKA64_INC = a copy of devkitA64's aarch64-none-elf/include)
#   build.sh switch      GEN OUTDIR          -> inside devkitpro/devkita64: GCC variants + main, links every .o in OUTDIR into s0bench.nro
# Variant tag -> compiler and flags (all use the S0b codegen x360_work/ppc_opt, non-volatile memory access, -fno-strict-aliasing):
#   g2 GCC -O2 | g3 GCC -O3 | g2nf GCC -O2 without flush-to-zero switching | c2 clang -O2 | c3 clang -O3
#   base/opt/optnv: the S0b variants (GCC -O2; optnv non-volatile)
set -e
target=$1; gen=$2; out=$3; extra=$4
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../.." && pwd)
simde=${SIMDE:-/simde}
CNT=""; [ "$extra" = count ] && CNT="-DS0_COUNT"
INC="-I$repo/tools/s0bench -I$repo/x360_work/ppc -I$simde"
mkdir -p "$out"

# per-variant: compiler family (g|c), optimisation level, header dir (novol|noftz|"")
variant() {
  case $1 in
    g2) echo "g -O2 novol" ;; g3) echo "g -O3 novol" ;; g2nf) echo "g -O2 noftz" ;;
    c2) echo "c -O2 novol" ;; c3) echo "c -O3 novol" ;;
    optnv) echo "g -O2 novol" ;; *) echo "g -O2 none" ;;
  esac
}

case $target in
  pc)     GXX=g++; CLX=clang++; ARCHF=""; SW="" ;;
  arm)    GXX=g++; CLX=clang++; ARCHF="-mcpu=cortex-a57"; SW="" ;;
  switch) export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH
          GXX=aarch64-none-elf-g++; CLX=""; ARCHF="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -ffunction-sections"
          SW="-D__SWITCH__ -I/opt/devkitpro/libnx/include" ;;
  switchclang)
          GXX=""; CLX=clang++; D=${DKA64_INC:?set DKA64_INC}
          ARCHF="--target=aarch64-none-elf -mcpu=cortex-a57 -fPIE -ffunction-sections -nostdinc++ -isystem $D/c++/15.2.0 -isystem $D/c++/15.2.0/aarch64-none-elf -isystem $D"
          SW="-D__SWITCH__" ;;
esac

for d in "$gen"/*/; do
  tag=$(basename "$d"); set -- $(variant "$tag"); fam=$1; opt=$2; hdr=$3
  if [ "$fam" = c ]; then cxx=$CLX; else cxx=$GXX; fi
  [ -n "$cxx" ] || continue                       # this step does not build this family
  [ "$target" = switchclang ] && [ "$fam" != c ] && continue
  [ "$target" = switch ] && [ "$fam" = c ] && continue
  flags="-std=gnu++20 -w $opt $ARCHF $SW $CNT"
  [ "$fam" = g ] && flags="$flags -include $repo/tools/switch_build/gcc_compat.h"
  hinc=""; [ "$hdr" != none ] && hinc="-I$repo/tools/s0bench/$hdr" && flags="$flags -fno-strict-aliasing"
  for f in "$d"chunk_*.cpp "$d"table.cpp; do $cxx $flags $hinc $INC -c "$f" -o "$out/${tag}_$(basename "$f").o" & done
done
wait
[ "$target" = switchclang ] && exit 0

MAINF="-std=gnu++20 -w -O2 $ARCHF $SW $CNT -include $repo/tools/switch_build/gcc_compat.h"
$GXX $MAINF $INC -c "$here/main.cpp" -o "$out/main.o"
if [ $target = switch ]; then
  $GXX -specs=/opt/devkitpro/libnx/switch.specs -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE "$out"/*.o -L/opt/devkitpro/libnx/lib -lnx -o "$out/s0bench.elf"
  nacptool --create "MGRR S0 bench" "mgr-nx" "0.3" "$out/s0bench.nacp"
  elf2nro "$out/s0bench.elf" "$out/s0bench.nro" --nacp="$out/s0bench.nacp"
else
  name=s0_bench; [ "$extra" = count ] && name=s0_count
  $GXX "$out"/*.o -o "$out/$name"
fi
