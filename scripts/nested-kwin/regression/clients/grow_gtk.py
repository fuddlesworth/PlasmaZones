#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# grow_gtk.py TITLE W H GROW_W GROW_H DELAY_MS — a plain GTK4 test window
# (app id org.pz.plain) that, DELAY_MS after mapping, raises its minimum size
# to GROW_W x GROW_H, so it commits a frame larger than the one the compositor
# gave it: the client-side growth (an oversized ack) of F248.
import sys

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gio, GLib, Gtk  # noqa: E402

TITLE = sys.argv[1] if len(sys.argv) > 1 else "grow"
WIDTH = int(sys.argv[2]) if len(sys.argv) > 2 else 400
HEIGHT = int(sys.argv[3]) if len(sys.argv) > 3 else 300
GROW_W = int(sys.argv[4]) if len(sys.argv) > 4 else 900
GROW_H = int(sys.argv[5]) if len(sys.argv) > 5 else 500
DELAY = int(sys.argv[6]) if len(sys.argv) > 6 else 4000


def on_activate(app):
    win = Gtk.ApplicationWindow(application=app, title=TITLE)
    win.set_default_size(WIDTH, HEIGHT)
    area = Gtk.DrawingArea()
    win.set_child(area)

    def grow():
        area.set_size_request(GROW_W, GROW_H)
        return False

    GLib.timeout_add(DELAY, grow)
    win.present()


app = Gtk.Application(application_id="org.pz.plain", flags=Gio.ApplicationFlags.NON_UNIQUE)
app.connect("activate", on_activate)
app.run([])
