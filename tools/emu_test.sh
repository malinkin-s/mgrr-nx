#!/bin/sh
# Graphics test bench (PLAN.md T4): replays a GPU recording made on PC through the Switch renderer in Ryujinx and
# compares each frame with what the PC's reference renderer showed for it. The console stays the target: this only
# filters out errors before a console run (the same NRO replays there with --mgrr_replay).
#
#   tools/emu_test.sh build/traces/NAME.gtr [PASSES [STEP_FRAME]]
#
# With STEP_FRAME, that frame of the last pass is also saved after each of its resolves (step<NN>.png, the copies in
# report.txt, with every draw): where an image goes wrong within the frame. PROBES=N,M (environment) also saves the
# color target after those draws (probe<N>.png); PLAIN=N,M draws those without depth/stencil test, blending and alpha
# test. DUMP=F instead dumps every resolved texture of frame F at full
# precision (the renderer's frame dump, dump/; not with STEP_FRAME). EXTRA='name = value\n...' adds settings.
# The recording comes from the Linux build: tools/run_linux.sh --gpu_trace_path=$PWD/build/traces/NAME.gtr
# --gpu_trace_start=N --gpu_trace_frames=K (it writes NAME.gtr.shaders and NAME.gtr.frame<F>.ppm next to it).
# Needs Ryujinx and its data in build/emu (NOTES.md, test bench) and build/switch/mgrr_clang.nro.
# Output: build/emu_runs/<time>/ with the replay's PNGs and report, the comparisons (compare_frame<F>.png:
# reference | replay | difference) and summary.txt.
set -e
repo=$(cd "$(dirname "$0")/.." && pwd)
trace=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
passes=${2:-3}
steps=${3:--1}
ryujinx=$repo/build/emu/flatpak/files/bin/Ryujinx
data=$repo/build/emu/data
sd=$data/sdcard/switch/mgrr
nro=$repo/build/switch/mgrr_clang.nro
[ -f "$trace" ] && [ -f "$trace.shaders" ] || { echo "no $trace or its .shaders"; exit 2; }
[ -x "$ryujinx" ] && [ -f "$data/system/prod.keys" ] || { echo "no Ryujinx in build/emu (NOTES.md)"; exit 2; }
[ -f "$nro" ] || { echo "no $nro"; exit 2; }

run=$repo/build/emu_runs/$(date +%Y%m%d_%H%M%S)
mkdir -p "$run" "$sd/traces"
name=$(basename "$trace")
rm -rf "$sd/replay_out" "$sd/traces"/* "$sd"/volcado_*
cp "$nro" "$sd/mgrr_clang.nro"
[ -f "$repo/build/shaders/mgrr_shaders.mgsl" ] && cp "$repo/build/shaders/mgrr_shaders.mgsl" "$sd/"
cp "$trace" "$trace.shaders" "$sd/traces/"
cat > "$sd/mgrr.toml" <<EOF
# Written by tools/emu_test.sh (emulator SD card only): replay instead of the game.
mgrr_replay = "sdmc:/switch/mgrr/traces/$name"
mgrr_replay_out = "sdmc:/switch/mgrr/replay_out"
mgrr_replay_passes = $passes
mgrr_replay_steps = $steps
mgrr_replay_probes = "${PROBES:-}"
mgrr_replay_plain = "${PLAIN:-}"
mgrr_replay_dump = ${DUMP:--1}
$(printf '%b' "${EXTRA:-}")
EOF

echo "replaying $name in Ryujinx ($passes passes)..."
status=0
timeout 600 "$ryujinx" --no-gui --root-data-dir "$data" --graphics-backend Vulkan --disable-docked-mode \
  --resolution-scale 1 "$sd/mgrr_clang.nro" > "$run/ryujinx.txt" 2>&1 || status=$?
cp -r "$sd/replay_out/." "$run/" 2>/dev/null || true
for d in "$sd"/volcado_*; do [ -d "$d" ] && cp -r "$d" "$run/dump"; done
log=$(ls -t "$sd/logs"/*.log 2>/dev/null | head -1)
[ -n "$log" ] && cp "$log" "$run/mgrr.log"

{
  echo "recording: $trace"
  echo "nro: $(md5sum "$nro" | cut -c1-8), Ryujinx exit $status"
  grep -a 'Unhandled exception caught' "$run/ryujinx.txt" | head -1 || true
  [ -f "$run/report.txt" ] && grep -E "^pass $passes |^replay done|no |not |cannot|truncated" "$run/report.txt"
  for png in "$run"/pass${passes}_frame*.png; do
    [ -f "$png" ] || { echo "no frames from the last pass"; break; }
    f=${png##*_frame}; f=${f%.png}
    ref=$trace.frame$f.ppm
    if [ -f "$ref" ]; then
      echo "frame $f: $(python3 -I "$repo/tools/imgdiff.py" "$ref" "$png" "$run/compare_frame$f.png")"
    else
      echo "frame $f: no reference image"
    fi
  done
} | tee "$run/summary.txt"
echo "-> $run"
