// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Launcher.LauncherResultRow, one result in the launcher list.
//
// Icon, title, subtitle, and, on the selected row, the action hint
// ("↵ Open") so the user knows what Enter will do before pressing it.
// The row is a LauncherModel delegate and reads its roles as required
// properties; it does not know which provider produced it.
//
// Kirigami.Icon draws its own fallback glyph for a name the icon theme
// cannot resolve (an app id used as an icon name, say), so a row never
// loses its icon slot.

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

    // Set by the ListView from isCurrentItem.
    property bool current: false

    signal clicked

    // Kept in step with the launcher's own _rowHeight, which derives the
    // list's height cap from it.
    implicitHeight: 56

    Accessible.role: Accessible.ListItem
    Accessible.name: root.subtitle.length > 0 ? qsTr("%1, %2").arg(root.title).arg(root.subtitle) : root.title
    // Announced as selected, and activatable. Without the press action a
    // screen-reader user could read a result and had no way to open it:
    // the row is not focusable (the text field keeps the keyboard, and the
    // list is driven from there), so nothing else carried the action.
    Accessible.selected: root.current
    Accessible.onPressAction: root.clicked()

    Rectangle {
        id: surface

        anchors.fill: parent
        anchors.leftMargin: Tokens.spacing_xs
        anchors.rightMargin: Tokens.spacing_xs
        radius: Tokens.radius_m
        // The mockup's selection ring: the selected row gets the tonal
        // container; the rest are transparent so the list reads as one
        // surface. primary_container / on_primary_container, because that
        // is a pair this theme defines; it has secondary_container but no
        // on_secondary_container, and a missing token reads undefined.
        color: root.current ? Theme.primary_container : "transparent"

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_short_2
                easing: Motion.standard
            }
        }

        readonly property color contentColor: root.current ? Theme.on_primary_container : Theme.on_surface
        readonly property color mutedColor: root.current ? Theme.on_primary_container : Theme.on_surface_variant

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Tokens.spacing_m
            anchors.rightMargin: Tokens.spacing_m
            spacing: Tokens.spacing_m

            Kirigami.Icon {
                source: root.iconName
                implicitWidth: 28
                implicitHeight: 28
                Layout.alignment: Qt.AlignVCenter
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    Accessible.ignored: true
                    text: root.title
                    // Titles and subtitles carry window titles and clipboard
                    // contents, so they are arbitrary text from other clients.
                    // AutoText would run Qt's rich-text heuristic over that and
                    // let markup restyle the row or reference local files.
                    textFormat: Text.PlainText
                    color: surface.contentColor
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_body_l
                    font.weight: Tokens.font_weight_medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    Accessible.ignored: true
                    text: root.subtitle
                    textFormat: Text.PlainText
                    color: surface.mutedColor
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_body_s
                    elide: Text.ElideRight
                    visible: root.subtitle.length > 0
                    Layout.fillWidth: true
                }
            }

            // The action hint, selected row only. Alternate shown after
            // the primary when the row offers one.
            Text {
                Accessible.ignored: true
                visible: root.current
                text: root.hasAlternateAction ? qsTr("↵ %1 · Alt+↵ %2").arg(root.primaryActionLabel).arg(root.alternateActionLabel) : qsTr("↵ %1").arg(root.primaryActionLabel)
                textFormat: Text.PlainText
                color: surface.mutedColor
                font.family: Tokens.font_family
                font.pixelSize: Tokens.font_size_label_s
                // The action labels come from the provider and grow under
                // translation. Without a ceiling this squeezes the title
                // column, which is the opposite of the intended priority.
                elide: Text.ElideRight
                Layout.maximumWidth: root.width * 0.4
            }
        }

        PhosphorRipple {
            anchors.fill: parent
            radius: surface.radius
            rippleColor: surface.contentColor
            onTapped: root.clicked()
        }
    }
}
