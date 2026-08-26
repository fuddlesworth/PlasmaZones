#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture ONE output of the nested session as a PNG via ScreenShot2.

Usage: capture-output.py <OutputName> <out.png>   (source env.sh first)

Dependencies: python-dbus (dbus-python) and Pillow.

CAVEAT: this render BYPASSES the effect chain, so it shows raw committed
geometry, never effect-side suppression. Use it to verify geometry, not
rendering."""
import os
import sys

import dbus
from PIL import Image

if len(sys.argv) != 3:
    sys.exit(__doc__)
name, out = sys.argv[1], sys.argv[2]

# Guard against silently capturing the HOST session: env.sh pins the nested
# bus address, and running without it would connect to the host bus, where
# ScreenShot2 also exists and answers with a plausible but meaningless PNG.
nest = os.environ.get("PZ_NESTED_DIR")
if nest:
    try:
        with open(os.path.join(nest, "env.sh"), encoding="utf-8") as f:
            env_sh = f.read()
    except OSError:
        sys.exit(f"cannot read {nest}/env.sh; is the nested session up?")
    addr = os.environ.get("DBUS_SESSION_BUS_ADDRESS", "")
    if not addr or f"'{addr}'" not in env_sh:
        sys.exit(
            "DBUS_SESSION_BUS_ADDRESS does not match the nested session's "
            f"env.sh; source {nest}/env.sh first or the capture hits the HOST"
        )
elif not os.environ.get("PZ_NESTED_REPO"):
    sys.exit("source the nested session's env.sh first (PZ_NESTED_DIR is unset)")

bus = dbus.SessionBus()
ss = bus.get_object("org.kde.KWin.ScreenShot2", "/org/kde/KWin/ScreenShot2")
iface = dbus.Interface(ss, "org.kde.KWin.ScreenShot2")
r, w = os.pipe()
opts = dbus.Dictionary({"native-resolution": dbus.Boolean(True)}, signature="sv")
res = iface.CaptureScreen(name, opts, dbus.types.UnixFd(w))
os.close(w)
width, height = int(res["width"]), int(res["height"])
fmt = res.get("format")
stride = int(res.get("stride", width * 4))
# QImage::Format_ARGB32 = 5, Format_ARGB32_Premultiplied = 6, Format_RGB32
# = 4 — the little-endian byte order for all three is BGRA, which is what
# the decoder below assumes. An ABSENT key is tolerated (older replies);
# anything else, including an explicit Format_Invalid (0), would decode
# into a colour-swapped or sheared image, so bail loudly instead.
if fmt is not None and int(fmt) not in (4, 5, 6):
    sys.exit(f"unsupported image format {int(fmt)} from ScreenShot2 (expected a 32-bit BGRA layout)")
data = b""
while True:
    chunk = os.read(r, 1 << 20)
    if not chunk:
        break
    data += chunk
os.close(r)
img = Image.frombytes("RGBA", (width, height), data, "raw", "BGRA", stride)
img.convert("RGB").save(out)
print(f"saved {out} {width}x{height}")
