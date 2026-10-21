#!/bin/sh
# Send the Switch build over Wi-Fi and run it, with the log streamed back here (game/src/mgrr_nxlink.cpp).
#
#   tools/nxlink.sh [SWITCH_IP] [-- cvar args for the game, e.g. --mgrr_overlay=false]
#
# On the Switch: hbmenu, press Y (netloader). Without SWITCH_IP (or $NXLINK_IP) nxlink looks for the console by
# UDP broadcast, which a firewall or VPN on this PC can stop; the IP is shown on the netloader screen.
# The log server needs TCP 28771 open for the LAN (ufw: sudo ufw allow from 192.168.0.0/24 to any port 28771 proto tcp).
# Only the NRO is sent: mgrr_shaders.mgsl and game/ must already be in sdmc:/switch/mgrr/.
# NRO=... picks another file (default out/sd/switch/mgrr/mgrr_clang.nro).
# Each run gets build/nxlink_logs/<time>/: log.txt and the screenshots (L+R+Minus, and every 30 s; tools/nxlink_recv.py).
set -e
repo=$(cd "$(dirname "$0")/.." && pwd)
nxlink=$repo/build/tools/nxlink
nro=${NRO:-$repo/out/sd/switch/mgrr/mgrr_clang.nro}

if [ ! -x "$nxlink" ]; then
  mkdir -p "$repo/build/tools"
  docker run --rm -u "$(id -u):$(id -g)" -v "$repo/build/tools:/out" devkitpro/devkita64 \
    cp /opt/devkitpro/tools/bin/nxlink /out/
fi

ip=${NXLINK_IP:-}
if [ $# -gt 0 ] && [ "$1" != "--" ]; then ip=$1; shift; fi
[ "${1:-}" = "--" ] && shift

run=$repo/build/nxlink_logs/$(date +%Y%m%d_%H%M%S)
mkdir -p "$run"
echo "sending $(basename "$nro") ($(du -h "$nro" | cut -f1)) to ${ip:-broadcast}; log and screenshots: $run; Ctrl+C to stop"

"$nxlink" -s -p "mgrr/$(basename "$nro")" ${ip:+-a "$ip"} "$nro" "$@" 2>&1 | python3 -I "$repo/tools/nxlink_recv.py" "$run"
