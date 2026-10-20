#!/bin/sh
# Run the Linux build (build/linux/mgrr) on x360_extracted/ with the Xenia GPU plugin.
# Keyboard = gamepad (--mnk_mode): Enter/X Start, Space A, Backspace B, WASD left stick; a real gamepad works via SDL.
#   tools/run_linux.sh [extra rexglue options...]      log: build/run.log
repo=$(cd "$(dirname "$0")/.." && pwd)
export LD_LIBRARY_PATH="$repo/sdk/out/linux-amd64:$repo/build/linux${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$repo/build/linux/mgrr" --game_data_root="$repo/x360_extracted" --gpu_plugin=xenos --mnk_mode=true \
     --log_file="$repo/build/run.log" "$@"
