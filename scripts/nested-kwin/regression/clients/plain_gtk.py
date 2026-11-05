#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# plain_gtk.py TITLE [W H] — a plain GTK4 test window (app id org.pz.plain)
# whose caption is TITLE, for the nested-KWin probes.
import sys

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gio, Gtk  # noqa: E402

TITLE = sys.argv[1] if len(sys.argv) > 1 else "plain"
WIDTH = int(sys.argv[2]) if len(sys.argv) > 2 else 500
HEIGHT = int(sys.argv[3]) if len(sys.argv) > 3 else 400


def on_activate(app):
    win = Gtk.ApplicationWindow(application=app, title=TITLE)
    win.set_default_size(WIDTH, HEIGHT)
    win.present()


app = Gtk.Application(application_id="org.pz.plain", flags=Gio.ApplicationFlags.NON_UNIQUE)
app.connect("activate", on_activate)
app.run([])
