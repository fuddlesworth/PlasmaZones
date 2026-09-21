// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Launcher.LauncherResultRow, one result in the launcher list.
//
// Glyph, title, subtitle, and, on the selected row, the action hint so
// the user knows what Enter will do. The selection is a 2 px blue line
// on the row's left edge that slides between rows (the launcher's only
// translating element, A3 §1); rows carry no filled background.
//
// Kirigami.Icon draws its own fallback glyph for a name the icon theme
// cannot resolve, so a row never loses its icon slot.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    required property int index
    required property string title
    required property string subtitle
    required property string iconName
    required property string primaryActionLabel
    required property string alternateActionLabel
    required property bool hasAlternateAction

    property bool current: false

    signal clicked

    implicitHeight: 44

    Accessible.role: Accessible.ListItem
    Accessible.name: root.subtitle.length > 0 ? qsTr("%1, %2").arg(root.title).arg(root.subtitle) : root.title
    Accessible.selected: root.current
    Accessible.onPressAction: root.clicked()

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: root.clicked()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Tokens.spacing_m
        anchors.rightMargin: Tokens.spacing_s
        spacing: Tokens.spacing_m

        Kirigami.Icon {
            source: root.iconName
            implicitWidth: 20
            implicitHeight: 20
            Layout.alignment: Qt.AlignVCenter
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Text {
                Accessible.ignored: true
                text: root.title
                textFormat: Text.PlainText
                color: Theme.on_surface
                opacity: root.current || hover.hovered ? 1 : 0.85
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_body_l
                font.weight: Tokens.font_weight_medium
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Text {
                Accessible.ignored: true
                text: root.subtitle
                textFormat: Text.PlainText
                color: Theme.on_surface_variant
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_body_s
                elide: Text.ElideRight
                visible: root.subtitle.length > 0
                Layout.fillWidth: true
            }
        }

        TabularText {
            Accessible.ignored: true
            visible: root.current
            text: root.hasAlternateAction ? qsTr("↵ %1 · Alt+↵ %2").arg(root.primaryActionLabel).arg(root.alternateActionLabel) : qsTr("↵ %1").arg(root.primaryActionLabel)
            textFormat: Text.PlainText
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_label_s
            elide: Text.ElideRight
            Layout.maximumWidth: root.width * 0.4
        }
    }
}
