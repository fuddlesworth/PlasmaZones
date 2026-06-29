// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.PhosphorTextField, M3 outlined text field.
//
// A single-line text input in a rounded outlined container. The outline
// thickens and tints to the primary colour on focus; a placeholder shows
// while the field is empty and unfocused.
//
//   PhosphorTextField {
//       placeholderText: i18n("Search")
//       onAccepted: launcher.run(text)
//   }
//
// Built on a bare TextInput inside a FocusScope so focus, theming, and
// the placeholder are fully under token control rather than inherited
// from a Controls style.

import QtQuick
import Phosphor.Theme

FocusScope {
    id: root

    property alias text: input.text
    property string placeholderText: ""
    property alias echoMode: input.echoMode
    property alias inputMethodHints: input.inputMethodHints

    // Emitted on Enter / Return.
    signal accepted

    implicitWidth: 220
    implicitHeight: 48

    Accessible.role: Accessible.EditableText
    Accessible.name: root.placeholderText

    readonly property bool _focused: input.activeFocus
    readonly property color _disabledTint: Qt.rgba(Theme.on_surface.r, Theme.on_surface.g, Theme.on_surface.b, StateLayer.disabled_content)

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: "transparent"
        border.width: root._focused ? 2 : 1
        border.color: !root.enabled ? Qt.rgba(Theme.on_surface.r, Theme.on_surface.g, Theme.on_surface.b, StateLayer.disabled_container) : (root._focused ? Theme.primary : Theme.outline)

        Behavior on border.color {
            ColorAnimation {
                duration: Motion.duration_short_2
                easing: Motion.standard
            }
        }
    }

    TextInput {
        id: input

        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        verticalAlignment: TextInput.AlignVCenter
        clip: true
        enabled: root.enabled
        color: root.enabled ? Theme.on_surface : root._disabledTint
        selectionColor: Theme.primary
        selectedTextColor: Theme.on_primary
        font.pixelSize: 14
        onAccepted: root.accepted()
    }

    Text {
        anchors.left: input.left
        anchors.verticalCenter: input.verticalCenter
        text: root.placeholderText
        color: Theme.on_surface_variant
        font.pixelSize: 14
        // Hidden once the user focuses the field or types anything, so it
        // never overlaps real input.
        visible: input.text.length === 0 && !root._focused
    }
}
