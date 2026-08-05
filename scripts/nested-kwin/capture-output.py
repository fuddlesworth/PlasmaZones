#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture ONE output of the nested session as a PNG via ScreenShot2.

Usage: capture-output.py <OutputName> <out.png>   (source env.sh first)

CAVEAT: this render BYPASSES the effect chain, so it shows raw committed
geometry, never effect-side suppression. Use it to verify geometry, not
rendering."""
import sys, os, dbus
name, out = sys.argv[1], sys.argv[2]
bus = dbus.SessionBus()
ss = bus.get_object("org.kde.KWin.ScreenShot2", "/org/kde/KWin/ScreenShot2")
iface = dbus.Interface(ss, "org.kde.KWin.ScreenShot2")
r, w = os.pipe()
opts = dbus.Dictionary({"native-resolution": dbus.Boolean(True)}, signature="sv")
res = iface.CaptureScreen(name, opts, dbus.types.UnixFd(w))
os.close(w)
width, height = int(res["width"]), int(res["height"])
data = b""
while True:
    chunk = os.read(r, 1 << 20)
    if not chunk:
        break
    data += chunk
os.close(r)
from PIL import Image
Image.frombytes("RGBA", (width, height), data, "raw", "BGRA").convert("RGB").save(out)
print(f"saved {out} {width}x{height}")
