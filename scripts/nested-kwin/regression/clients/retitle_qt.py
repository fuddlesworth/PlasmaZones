#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# retitle_qt.py TITLE CTLFILE [W H] — a PySide6 test window (app id
# org.pz.qtretitle) titled TITLE, server-side decorated (Qt asks KWin for a
# server frame, unlike GTK4's client-side one, so KWin's noBorder reads the
# title bar). Every 30 ms it reads CTLFILE, and when the content changes it
# retitles to the first line. Non-empty titles only: Qt substitutes the
# application name for an empty one. Run it without the nested session's
# QT_PLUGIN_PATH.
import os
import sys

from PySide6.QtCore import QTimer
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import QApplication, QWidget

TITLE = sys.argv[1] if len(sys.argv) > 1 else "qtretitle"
CTL = sys.argv[2] if len(sys.argv) > 2 else "/dev/null"
WIDTH = int(sys.argv[3]) if len(sys.argv) > 3 else 500
HEIGHT = int(sys.argv[4]) if len(sys.argv) > 4 else 400


def read_ctl():
    try:
        with open(CTL) as f:
            return f.read()
    except OSError:
        return None


if os.path.exists(CTL) and CTL != "/dev/null":
    os.remove(CTL)
app = QApplication([sys.argv[0]])
QGuiApplication.setDesktopFileName("org.pz.qtretitle")
win = QWidget()
win.setWindowTitle(TITLE)
win.resize(WIDTH, HEIGHT)
state = {"seen": read_ctl()}


def poll():
    content = read_ctl()
    if content is not None and content != state["seen"]:
        state["seen"] = content
        title = content.split("\n", 1)[0]
        if title:
            win.setWindowTitle(title)


timer = QTimer()
timer.timeout.connect(poll)
timer.start(30)
win.show()
app.exec()
