#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# retitle_gtk.py TITLE CTLFILE [W H] — a plain GTK4 test window (app id
# org.pz.plain) titled TITLE that retitles itself on demand: every 30 ms it
# reads CTLFILE, and when the file's content changes it sets its title to the
# first line (an empty first line sets the title to ""). Write the new title
# with `printf '%s\n' T2 > CTLFILE`. A missing CTLFILE changes nothing.
import os
import sys

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gio, GLib, Gtk  # noqa: E402

TITLE = sys.argv[1] if len(sys.argv) > 1 else "retitle"
CTL = sys.argv[2] if len(sys.argv) > 2 else "/dev/null"
WIDTH = int(sys.argv[3]) if len(sys.argv) > 3 else 500
HEIGHT = int(sys.argv[4]) if len(sys.argv) > 4 else 400


def read_ctl():
    try:
        with open(CTL) as f:
            return f.read()
    except OSError:
        return None


def on_activate(app):
    win = Gtk.ApplicationWindow(application=app, title=TITLE)
    win.set_default_size(WIDTH, HEIGHT)
    state = {"seen": read_ctl()}

    def poll():
        content = read_ctl()
        if content is not None and content != state["seen"]:
            state["seen"] = content
            win.set_title(content.split("\n", 1)[0])
        return True

    GLib.timeout_add(30, poll)
    win.present()


if os.path.exists(CTL) and CTL != "/dev/null":
    os.remove(CTL)
app = Gtk.Application(application_id="org.pz.plain", flags=Gio.ApplicationFlags.NON_UNIQUE)
app.connect("activate", on_activate)
app.run([])
