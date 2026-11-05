#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# dropproxy_owner.py SCREEN JSON — register a WindowDrag drop proxy for SCREEN on the session bus,
# print "registered <unique bus name>" once the call returns, then hold the connection open until
# killed. The daemon ties the proxy to this connection, so killing the process is the owner going
# away (the Phosphor shell crashing with its placement map up).
import sys

from gi.repository import Gio, GLib

SCREEN = sys.argv[1]
PROXY = sys.argv[2]

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync("org.plasmazones", "/PlasmaZones", "org.plasmazones.WindowDrag", "registerDropProxy",
              GLib.Variant("(ss)", (SCREEN, PROXY)), None, Gio.DBusCallFlags.NONE, 5000, None)
print("registered", bus.get_unique_name(), flush=True)
GLib.MainLoop().run()
