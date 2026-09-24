// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Column {
    id: root
    required property var group
    property color accent: Appearance.stops[1]
    property var expandedRows: ({})
    signal expansionToggled(string id)
    spacing: 8
    RowLayout {
        width: root.width
        spacing: 8
        ShellIcon {
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
            source: ({
                    eye: "view-visible",
                    grid: "view-grid",
                    tune: "configure",
                    folder: "folder",
                    keyboard: "input-keyboard"
                })[root.group.icon] || "input-keyboard"
            color: root.accent
        }
        ShortcutText {
            Layout.fillWidth: true
            text: root.group.label
            font.weight: Font.Medium
        }
        ShortcutText {
            text: String(root.group.count)
            font.family: Tokens.font_family_mono
            muted: true
            size: 9
        }
    }
    Rectangle {
        width: root.width
        height: 1
        color: Appearance.outline
    }
    Column {
        width: root.width
        Repeater {
            model: root.group.rows
            delegate: Column {
                id: row
                required property var modelData
                width: root.width
                ShortcutEntry {
                    id: header
                    width: row.width
                    entry: row.modelData
                    familyHeader: !!row.modelData.family
                    accent: root.accent
                    expanded: !!root.expandedRows[row.modelData.id]
                    onExpansionToggled: root.expansionToggled(row.modelData.id)
                }
                Loader {
                    active: header.familyHeader && header.expanded
                    visible: active
                    width: row.width - 15
                    x: 15
                    sourceComponent: Item {
                        implicitHeight: members.implicitHeight
                        Rectangle {
                            x: -10
                            width: 1
                            height: parent.height
                            color: root.accent
                        }
                        Column {
                            id: members
                            width: parent.width
                            spacing: 2
                            Repeater {
                                model: row.modelData.children
                                delegate: ShortcutEntry {
                                    required property var modelData
                                    width: members.width
                                    entry: modelData
                                    accent: root.accent
                                    expanded: !!root.expandedRows[modelData.id]
                                    onExpansionToggled: root.expansionToggled(modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
