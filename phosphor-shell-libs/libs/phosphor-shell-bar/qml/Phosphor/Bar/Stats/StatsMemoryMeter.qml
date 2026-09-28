// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Row {
    id: root
    property real used: StatsModel.number(StatsModel.memory.used)
    property real cached: StatsModel.number(StatsModel.memory.cached)
    property real free: StatsModel.number(StatsModel.memory.free)
    property real total: Math.max(1, StatsModel.number(StatsModel.memory.total))
    height: 7
    spacing: 3
    Accessible.ignored: true
    Repeater {
        model: [root.used, root.cached, root.free]
        Rectangle {
            required property int index
            required property real modelData
            width: Math.max(0, root.width - root.spacing * 2) * Math.max(0, Math.min(1, modelData / root.total))
            height: root.height
            radius: 1
            color: index === 0 ? Appearance.stops[1] : index === 1 ? Qt.tint(Appearance.card, Qt.alpha(Appearance.stops[1], 0.35)) : Qt.tint(Appearance.recess, Qt.alpha(Appearance.muted, 0.15))
        }
    }
}
