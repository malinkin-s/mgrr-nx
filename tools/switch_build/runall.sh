#!/bin/sh
cd /ppc
ls ppc_recomp.*.cpp | sed 's#^#/ppc/#' | xargs -P6 -n1 python3 /cc.py -O2
