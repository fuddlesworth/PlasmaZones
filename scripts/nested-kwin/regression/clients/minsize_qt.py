#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# minsize_qt.py TITLE [W H MIN_W MIN_H] — a PySide6 test window (app id
# org.pz.qtmin) titled TITLE, server-side decorated (Qt asks KWin for a server
# frame, unlike GTK4's client-side one), that declares a minimum size of
# MIN_W x MIN_H. The effect reports that minimum as a FRAME size, so it moves
# whenever KWin adds or removes the title bar. Run it without the nested
# session's QT_PLUGIN_PATH.
import sys

from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import QApplication, QWidget

TITLE = sys.argv[1] if len(sys.argv) > 1 else "qtmin"
WIDTH = int(sys.argv[2]) if len(sys.argv) > 2 else 500
HEIGHT = int(sys.argv[3]) if len(sys.argv) > 3 else 400
MIN_WIDTH = int(sys.argv[4]) if len(sys.argv) > 4 else 300
MIN_HEIGHT = int(sys.argv[5]) if len(sys.argv) > 5 else 250

app = QApplication([sys.argv[0]])
QGuiApplication.setDesktopFileName("org.pz.qtmin")
win = QWidget()
win.setWindowTitle(TITLE)
win.setMinimumSize(MIN_WIDTH, MIN_HEIGHT)
win.resize(WIDTH, HEIGHT)
win.show()
app.exec()
