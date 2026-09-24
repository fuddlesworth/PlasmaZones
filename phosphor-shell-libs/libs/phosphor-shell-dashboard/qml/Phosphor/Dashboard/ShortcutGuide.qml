// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme

Rectangle {
    id: root
    property string scope: "tiling"
    property color accent: Appearance.stops[1]
    property var bindings: []
    readonly property bool illustrated: width > 680
    readonly property var content: scope === "scrolling" ? [qsTr("Scrolling / Field guide"), qsTr("Keep your place in the flow."), qsTr("Focus travels through the strip. Columns can share space, stack windows, or become tabs."), qsTr("Center column")] : scope === "snapping" ? [qsTr("Snapping / Field guide"), qsTr("Your layout. Your call."), qsTr("Send a window to a numbered zone, swap positions, or extend it across neighboring zones."), qsTr("Move to zone 1")] : [qsTr("Tiling / Field guide"), qsTr("A place for every window."), qsTr("Move focus without disturbing the layout. Master controls apply to layouts with a master area."), qsTr("Focus master")]
    implicitHeight: Math.max(Appearance.compact ? 136 : 155, copy.implicitHeight + 40)
    radius: Appearance.radius * .7
    color: Qt.tint(Appearance.recess, Qt.alpha(accent, .065))
    border.color: Qt.tint(Appearance.outline, Qt.alpha(accent, .2))
    RowLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 22
        ColumnLayout {
            id: copy
            Layout.fillWidth: true
            spacing: 6
            ShortcutText {
                Layout.fillWidth: true
                text: root.content[0].toLocaleUpperCase()
                kicker: true
                size: 9
            }
            ShortcutText {
                Layout.fillWidth: true
                text: root.content[1]
                size: 16
                font.weight: Font.Medium
            }
            ShortcutText {
                Layout.fillWidth: true
                text: root.content[2]
                muted: true
                size: 11
                lineHeight: 1.25
            }
            Flow {
                Layout.fillWidth: true
                Layout.topMargin: 5
                spacing: 8
                ShortcutKeys {
                    bindings: root.bindings
                    visible: bindings.length > 0
                    width: Math.min(implicitWidth, parent.width)
                }
                ShortcutText {
                    height: Math.max(24, implicitHeight)
                    verticalAlignment: Text.AlignVCenter
                    text: root.bindings.length ? root.content[3] : qsTr("%1 · Unassigned").arg(root.content[3])
                    size: 10
                    muted: true
                }
            }
        }
        Item {
            id: diagram
            visible: root.illustrated
            Layout.preferredWidth: visible ? 245 : 0
            Layout.preferredHeight: 112
            clip: true
            Accessible.role: Accessible.Graphic
            Accessible.name: root.scope === "tiling" ? qsTr("One master window beside two stacked windows") : root.scope === "snapping" ? qsTr("Four numbered zones with the first selected") : qsTr("A scrolling strip with the second column selected")
            Repeater {
                model: root.scope === "tiling" ? 3 : 4
                delegate: Rectangle {
                    id: tile
                    required property int index
                    readonly property bool selected: index === (root.scope === "scrolling" ? 1 : 0)
                    readonly property color tint: Appearance.windowColor(index)
                    x: root.scope === "scrolling" ? index * 97 - 42 : root.scope === "tiling" ? index ? 145 : 0 : (index % 2) * 126
                    y: root.scope === "scrolling" ? 8 : root.scope === "tiling" ? index === 2 ? 59 : 4 : Math.floor(index / 2) * 59
                    width: root.scope === "scrolling" ? 89 : root.scope === "tiling" ? index ? 98 : 139 : 119
                    height: root.scope === "scrolling" ? 90 : root.scope === "tiling" ? index ? 49 : 104 : 53
                    color: Qt.tint(Appearance.recess, Qt.alpha(tint, .13))
                    border.color: selected ? Appearance.text : Qt.alpha(tint, .55)
                    radius: Appearance.radius * .28
                    Rectangle {
                        x: 1
                        y: 1
                        width: parent.width - 2
                        height: 2
                        visible: tile.selected
                        radius: 1
                        color: root.accent
                    }
                    Rectangle {
                        x: 8
                        y: 8
                        width: parent.width * .28
                        height: 2
                        radius: 1
                        color: Qt.alpha(tile.tint, .6)
                    }
                    ShortcutText {
                        x: 8
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 6
                        text: root.scope === "tiling" && tile.index === 0 ? qsTr("Master") : String(tile.index + 1).padStart(2, "0")
                        size: 9
                        muted: !tile.selected
                    }
                }
            }
            Rectangle {
                visible: root.scope === "scrolling"
                x: 14
                y: 107
                width: parent.width - 28
                height: 3
                radius: 2
                color: Appearance.outline
                Rectangle {
                    x: parent.width * .15
                    width: parent.width * .47
                    height: parent.height
                    radius: 2
                    color: root.accent
                }
            }
        }
    }
}
