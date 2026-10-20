#!/bin/sh
f=$1; b=$(basename $f .hlsl)
case $b in *.vso) T=vs_6_0;; *) T=ps_6_0;; esac
if $X/bin/x64/dxc-linux -spirv -T $T -E main -fspv-target-env=vulkan1.2 -HV 2021 -Fo spv/$b.spv $f >spv/$b.log 2>&1; then echo "0 $b"; else echo "1 $b $(grep -m1 error spv/$b.log | cut -c1-140)"; fi
