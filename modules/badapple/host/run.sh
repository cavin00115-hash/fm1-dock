#!/bin/sh
# BADAPPLE on this computer: build/bahost/badapple.wav and screens (PNG). Needs a C compiler, Python 3 + Pillow.
#   modules/badapple/host/run.sh PACK.bin [seconds]
set -e
cd "$(dirname "$0")/../../.."
mkdir -p build/bahost
cc -O2 -Wall -Wno-unused-function -Wno-builtin-declaration-mismatch -DBA_HOST \
   -Dmemcpy=ba_memcpy -Dmemset=ba_memset -Dmemcmp=ba_memcmp \
   -Imodules/badapple -Ifirmware/dock -Imodules/lib -Imodules/fm6 -Imodules/fm6/melodee \
   -c modules/badapple/badapple.c -o build/bahost/badapple.o
cc -O2 -Wall -Ifirmware/dock -c modules/badapple/host/ba_host.c -o build/bahost/ba_host.o
cc build/bahost/badapple.o build/bahost/ba_host.o -o build/bahost/ba_host -lm
build/bahost/ba_host "$1" build/bahost "${2:-225}"
python3 - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/bahost").glob("screen-*.ppm")):
    Image.open(p).save(p.with_suffix(".png")); p.unlink()
PY
