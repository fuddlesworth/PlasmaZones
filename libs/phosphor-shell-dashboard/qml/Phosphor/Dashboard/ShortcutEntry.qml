// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Column {
    id: root
    required property var entry
    property color accent: Appearance.stops[1]
    property bool expanded: false
    property bool familyHeader: false
    signal expansionToggled
    width: 400
    spacing: 3
    objectName: "shortcutEntry:" + entry.id
    Basic.AbstractButton {
        id: summary
        objectName: "shortcutSummary:" + root.entry.id
        width: root.width
        implicitHeight: Math.max(Appearance.compact ? 35 : 42, line.implicitHeight + 14)
        leftPadding: 5
        rightPadding: 5
        topPadding: 7
        bottomPadding: 7
        Accessible.name: root.entry.label
        Accessible.description: (root.entry.triggers || []).join(qsTr(" or ")) || (root.familyHeader ? qsTr("Expand to see bindings") : root.entry.external ? qsTr("Set in compositor") : qsTr("Unassigned"))
        Accessible.role: Accessible.Button
        checkable: true
        checked: root.expanded
        onClicked: root.expansionToggled()
        background: Rectangle {
            radius: 5
            color: summary.hovered || root.expanded && !root.familyHeader ? Qt.alpha(Appearance.card, .6) : "transparent"
            border.color: summary.visualFocus ? root.accent : "transparent"
        }
        contentItem: RowLayout {
            id: line
            spacing: 10
            ShortcutText {
                Layout.fillWidth: true
                text: root.entry.label
            }
            ShellIcon {
                visible: root.familyHeader
                Layout.preferredWidth: visible ? 12 : 0
                Layout.preferredHeight: 12
                source: root.expanded ? "go-down" : "go-next"
                color: Appearance.muted
            }
            ShortcutKeys {
                id: keys
                objectName: "shortcutBindings:" + root.entry.id
                readonly property var parts: root.entry.keyParts || []
                bindings: parts.length ? [parts] : (root.entry.bindings || [])
                visible: bindings.length > 0
                Layout.preferredWidth: visible ? Math.min(naturalWidth, root.width * .62) : 0
                Layout.preferredHeight: implicitHeight
                keyColor: root.familyHeader ? root.accent : Appearance.text
            }
            ShortcutText {
                visible: !keys.visible
                Layout.maximumWidth: root.width * .4
                horizontalAlignment: Text.AlignRight
                muted: true
                size: 10
                text: root.familyHeader ? ((root.entry.children || []).some(row => row.assigned) ? qsTr("%1 bindings").arg(root.entry.children.length) : qsTr("Unassigned")) : root.entry.external ? qsTr("Set in compositor") : qsTr("Unassigned")
            }
        }
    }
    Loader {
        objectName: "shortcutDetails:" + root.entry.id
        width: root.width - 10
        x: 5
        active: root.expanded && !root.familyHeader
        visible: active
        sourceComponent: Rectangle {
            implicitHeight: detail.implicitHeight + 20
            radius: 5
            color: Qt.alpha(Appearance.recess, .65)
            border.color: Appearance.outline
            Column {
                id: detail
                x: 11
                y: 10
                width: parent.width - 22
                spacing: 7
                ShortcutText {
                    width: parent.width
                    text: root.entry.description || qsTr("Use this shortcut to %1.").arg(root.entry.label.toLocaleLowerCase())
                    muted: true
                    size: 10
                }
                ShortcutText {
                    width: parent.width
                    text: root.entry.id
                    font.family: Tokens.font_family_mono
                    muted: true
                    size: 9
                }
                ShortcutText {
                    width: parent.width
                    visible: !root.entry.assigned
                    text: root.entry.external ? qsTr("Assign this action in your compositor’s shortcut configuration.") : qsTr("Assign a shortcut in Phosphor settings.")
                    muted: true
                    size: 10
                }
            }
        }
    }
}
