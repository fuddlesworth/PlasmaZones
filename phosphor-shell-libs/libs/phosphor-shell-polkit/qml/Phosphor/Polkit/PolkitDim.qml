// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Rectangle {
    property bool active: true
    visible: active
    color: Appearance.light ? "#470f192a" : "#6b050a14"
    Accessible.ignored: true
}
