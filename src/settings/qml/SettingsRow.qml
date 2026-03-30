// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Two-line setting row with title, description, and a right-aligned control.
 *
 * The default children of this item become the right-side control widget.
 *
 * Usage:
 *   SettingsRow {
 *       title: i18n("Resolution")
 *       description: i18n("Resolution and refresh rate")
 *       ComboBox { model: ["1080p", "1440p", "4K"] }
 *   }
 */
Item {
    id: root

    // ── Public API ──────────────────────────────────────────────────────
    property string title: ""
    property string description: ""
    default property alias content: controlContainer.data

    Layout.fillWidth: true
    implicitWidth: rowLayout.implicitWidth
    implicitHeight: rowLayout.implicitHeight
    Accessible.name: root.title
    Accessible.description: root.description
    Accessible.role: Accessible.Row

    RowLayout {
        id: rowLayout

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Kirigami.Units.largeSpacing
        anchors.rightMargin: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        // Left side: title + description
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 10
            spacing: Kirigami.Units.smallSpacing / 2

            Label {
                text: root.title
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Label {
                text: root.description
                Layout.fillWidth: true
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                visible: root.description.length > 0
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

        }

        // Right side: control widget (default children)
        Row {
            id: controlContainer

            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
            Layout.maximumWidth: rowLayout.width * 0.45
            spacing: Kirigami.Units.smallSpacing
        }

    }

}
