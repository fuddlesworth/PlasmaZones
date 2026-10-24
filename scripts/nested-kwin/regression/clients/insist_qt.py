#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# insist_qt.py TITLE CTLFILE [HOLD_W HOLD_H] — a PySide6 test window (app id
# org.pz.insist) that holds its own size the way SDL games and some Wine apps
# answer every configure: whenever its size differs from the hold on an axis
# the hold names, it resizes itself back 50 ms later. A hold of 0 on an axis
# follows the compositor on that axis. It declares no minimum size, so a hold
# shows only in what it commits. Every 30 ms it reads CTLFILE, and when the
# content changes it takes the hold from the first line ("W H"). After every
# resize it writes its own size as "W H" to CTLFILE.size, so a probe can read
# the client size behind its frame. QT_QPA_PLATFORM picks the platform
# (wayland, or xcb for Xwayland). Run it without the nested session's
# QT_PLUGIN_PATH.
import os
import sys

from PySide6.QtCore import QTimer
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import QApplication, QWidget

TITLE = sys.argv[1] if len(sys.argv) > 1 else "insist"
CTL = sys.argv[2] if len(sys.argv) > 2 else "/dev/null"
HOLD = [int(sys.argv[3]) if len(sys.argv) > 3 else 0, int(sys.argv[4]) if len(sys.argv) > 4 else 0]
SIZE = CTL + ".size" if CTL != "/dev/null" else None


def read_ctl():
    try:
        with open(CTL) as f:
            return f.read()
    except OSError:
        return None


def write_size(w, h):
    if SIZE is None:
        return
    tmp = SIZE + ".tmp"
    with open(tmp, "w") as f:
        f.write("%d %d\n" % (w, h))
    os.replace(tmp, SIZE)


class Win(QWidget):
    def resizeEvent(self, event):
        super().resizeEvent(event)
        write_size(self.width(), self.height())
        if held() != (self.width(), self.height()):
            reassert.start()


def held():
    return (HOLD[0] or win.width(), HOLD[1] or win.height())


def insist():
    w, h = held()
    if (w, h) != (win.width(), win.height()):
        win.resize(w, h)


if CTL != "/dev/null":
    for p in (CTL, SIZE):
        if os.path.exists(p):
            os.remove(p)
app = QApplication([sys.argv[0]])
QGuiApplication.setDesktopFileName("org.pz.insist")
reassert = QTimer()
reassert.setSingleShot(True)
reassert.setInterval(50)
reassert.timeout.connect(insist)
win = Win()
win.setWindowTitle(TITLE)
win.resize(HOLD[0] or 400, HOLD[1] or 300)
state = {"seen": read_ctl()}


def poll():
    content = read_ctl()
    if content is None or content == state["seen"]:
        return
    state["seen"] = content
    parts = content.split("\n", 1)[0].split()
    if len(parts) == 2 and all(p.isdigit() for p in parts):
        HOLD[0], HOLD[1] = int(parts[0]), int(parts[1])
        insist()


timer = QTimer()
timer.timeout.connect(poll)
timer.start(30)
win.show()
write_size(win.width(), win.height())
app.exec()
