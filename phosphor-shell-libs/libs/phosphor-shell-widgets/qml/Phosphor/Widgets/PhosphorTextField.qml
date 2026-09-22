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

    // Tab-focusable when enabled; focus lands on the inner TextInput (which
    // declares focus: true within this scope) so typing works immediately.
    activeFocusOnTab: enabled

    Accessible.role: Accessible.EditableText
    // Announce the entered text once there is any, falling back to the
    // placeholder while empty. Password fields keep the placeholder so the
    // secret is never read out by assistive tech.
    Accessible.name: (input.text !== "" && input.echoMode === TextInput.Normal) ? input.text : root.placeholderText

    readonly property bool _focused: input.activeFocus
    readonly property color _disabledTint: StateLayer.disabledContent(Theme.on_surface)

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_s
        color: "transparent"
        border.width: root._focused ? 2 : 1
        border.color: !root.enabled ? StateLayer.disabledContainer(Theme.on_surface) : (root._focused ? Theme.primary : Theme.outline)

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
        anchors.leftMargin: Tokens.spacing_l
        anchors.rightMargin: Tokens.spacing_l
        verticalAlignment: TextInput.AlignVCenter
        clip: true
        enabled: root.enabled
        // The focused item within the scope, so the field's activeFocusOnTab
        // delegates Tab focus here.
        focus: true
        color: root.enabled ? Theme.on_surface : root._disabledTint
        selectionColor: Theme.primary
        selectedTextColor: Theme.on_primary
        font.pixelSize: Tokens.font_size_body_l
        onAccepted: root.accepted()
    }

    Text {
        anchors.left: input.left
        anchors.right: input.right
        anchors.verticalCenter: input.verticalCenter
        text: root.placeholderText
        // Dim with the field when disabled, matching the border and input.
        color: root.enabled ? Theme.on_surface_variant : root._disabledTint
        font.pixelSize: Tokens.font_size_body_l
        // Clip like the input (which sets clip: true) so a long placeholder
        // doesn't spill past the rounded outline.
        elide: Text.ElideRight
        // Hidden once the user focuses the field or types anything, so it
        // never overlaps real input.
        visible: input.text.length === 0 && !root._focused
    }
}
