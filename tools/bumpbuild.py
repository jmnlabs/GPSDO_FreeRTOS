#!/usr/bin/env python3
"""bumpbuild.py — increment BUILD_SERIAL in gpsdo_build_id.h.

WHY THIS EXISTS
---------------
The Arduino builder skips any translation unit whose sources have not changed,
and __DATE__/__TIME__ live in the sketch. Edit gpsdo_algorithms.cpp, upload, and
the banner still reports the compile time of whenever the sketch itself last
changed — which on 26.08 meant two captures from two different builds carrying
the same stamp, and an hour spent arguing with a log that was right.

The sketch includes gpsdo_build_id.h, so touching that file is enough to make the
builder recompile the sketch and nothing else. Run this before a compile and the
timestamp in the banner is the truth.

It is a convenience, not the guarantee. The banner also prints a CRC-32 of the
flash image, computed at boot from the flash itself, and that one cannot be
stale whether this script was run or not.

Usage:
    python3 tools/bumpbuild.py [path-to-sketch-dir]
"""
import re
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else here.parent
path = root / "gpsdo_build_id.h"

if not path.exists():
    sys.exit(f"bumpbuild: no gpsdo_build_id.h in {root}")

text = path.read_text(encoding="utf-8")
m = re.search(r"^(#define\s+BUILD_SERIAL\s+)(\d+)\s*$", text, re.M)
if not m:
    sys.exit("bumpbuild: no '#define BUILD_SERIAL <n>' line in gpsdo_build_id.h")

n = int(m.group(2)) + 1
path.write_text(text[:m.start()] + f"{m.group(1)}{n}" + text[m.end():],
                encoding="utf-8")
print(f"bumpbuild: BUILD_SERIAL -> {n}")
