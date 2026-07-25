// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.BarIconButton, a round icon-only bar button.
//
// Shared chrome for the bar's trailing buttons (control center,
// notifications, power): a circular hit target with an M3 hover/pressed
// state layer, a themed icon from the icon theme, an optional unread
// badge, and an `activated` signal. Keyboard and pointer paths match. A
// fallback dot keeps the button visible if the icon theme has no glyph
// for the requested name, so a missing icon never yields an invisible
// control.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme

Item {
    id: root

    // Freedesktop icon name resolved through the icon-theme provider.
    property string iconName: ""
    // Accessible name (also the implicit tooltip intent).
    property string label: ""
    // Unread/attention count; 0 hides the badge.
    property int badgeCount: 0

    // Emitted on a completed activation (tap, Space, Enter) when enabled.
    // The seam the popout surfaces (Phase 4.3/4.4/4.6) bind to.
    signal activated

    implicitWidth: 28
    implicitHeight: 28

    Accessible.role: Accessible.Button
    Accessible.name: root.label

    activeFocusOnTab: enabled
    Keys.onSpacePressed: event => root._activateFromKey(event)
    Keys.onReturnPressed: event => root._activateFromKey(event)
    Keys.onEnterPressed: event => root._activateFromKey(event)

    function _activateFromKey(event) {
        if (event.isAutoRepeat || !root.enabled)
            return;
        root.activated();
    }

    Rectangle {
        id: chip

        anchors.fill: parent
        radius: height / 2
        color: !root.enabled ? "transparent" : mouse.pressed ? Qt.alpha(Theme.on_surface, StateLayer.pressed) : (mouse.containsMouse || root.activeFocus) ? Qt.alpha(Theme.on_surface, StateLayer.hover) : "transparent"

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_short_2
                easing: Motion.standard
            }
        }

        Kirigami.Icon {
            anchors.centerIn: parent
            width: 18
            height: 18
            source: root.iconName
            // Tint to a single foreground colour so the glyph reads on the
            // dark capsule regardless of the theme's light/dark icon
            // variant; Kirigami draws its own fallback for an unknown name.
            isMask: true
            color: Theme.on_surface_variant
        }

        MouseArea {
            id: mouse

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            enabled: root.enabled
            onClicked: root.activated()
        }
    }

    Rectangle {
        id: badge

        visible: root.badgeCount > 0
        anchors.right: parent.right
        anchors.top: parent.top
        width: Math.max(height, badgeLabel.implicitWidth + Tokens.spacing_xs)
        height: 14
        radius: height / 2
        color: Theme.error

        Text {
            id: badgeLabel

            anchors.centerIn: parent
            text: root.badgeCount > 99 ? "99+" : root.badgeCount.toString()
            color: Theme.on_error
            font.pixelSize: Tokens.font_size_label_s
            font.weight: Tokens.font_weight_medium
            font.family: Tokens.font_family
        }
    }
}
