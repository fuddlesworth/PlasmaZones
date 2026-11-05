#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# appid_gtk.py TITLE APPID [W H] — a plain GTK4 test window like plain_gtk.py
# but with its own app id, for probes that need windows of different apps.
import sys

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gio, Gtk  # noqa: E402

TITLE = sys.argv[1] if len(sys.argv) > 1 else "appid"
APPID = sys.argv[2] if len(sys.argv) > 2 else "org.pz.appid"
WIDTH = int(sys.argv[3]) if len(sys.argv) > 3 else 300
HEIGHT = int(sys.argv[4]) if len(sys.argv) > 4 else 300


def on_activate(app):
    win = Gtk.ApplicationWindow(application=app, title=TITLE)
    win.set_default_size(WIDTH, HEIGHT)
    win.present()


app = Gtk.Application(application_id=APPID, flags=Gio.ApplicationFlags.NON_UNIQUE)
app.connect("activate", on_activate)
app.run([])
