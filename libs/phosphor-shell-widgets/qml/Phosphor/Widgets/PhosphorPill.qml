// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.PhosphorPill, M3 chip / toggle pill.
//
// A compact fully-rounded label that doubles as a selectable chip. In
// the resting state it is an outlined chip; when `selected` it fills
// with the secondary container. Use it for filter chips, quick toggles,
// and bar segments.
//
//   PhosphorPill {
//       text: i18n("Wi-Fi")
//       selected: network.enabled
//       onToggled: network.toggle()
//   }
//
// `clicked` fires on every tap; `toggled` is a convenience alias for
// hosts that drive a boolean. The pill does NOT flip `selected` itself,
// the host owns that state so it stays a single source of truth.

import QtQuick
import Phosphor.Theme

Item {
    id: root

    property string text: ""
    property bool selected: false

    signal clicked
    signal toggled

    implicitHeight: 32
    implicitWidth: label.implicitWidth + Tokens.spacing_xl

    Accessible.role: Accessible.CheckBox
    Accessible.name: root.text
    Accessible.checked: root.selected
    Accessible.onPressAction: if (root.enabled) {
        root.clicked();
        root.toggled();
    }

    readonly property color _container: {
        if (!enabled)
            return Qt.rgba(Theme.on_surface.r, Theme.on_surface.g, Theme.on_surface.b, StateLayer.disabled_container);
        return selected ? Theme.secondary_container : "transparent";
    }

    readonly property color _content: {
        if (!enabled)
            return Qt.rgba(Theme.on_surface.r, Theme.on_surface.g, Theme.on_surface.b, StateLayer.disabled_content);
        return selected ? Theme.on_surface : Theme.on_surface_variant;
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root._container
        // Outline only in the unselected, enabled resting state; a
        // filled or disabled pill carries no border.
        border.width: !root.selected && root.enabled ? 1 : 0
        border.color: Theme.outline_variant

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }
        }

        PhosphorRipple {
            anchors.fill: parent
            radius: parent.radius
            interactive: root.enabled
            rippleColor: root._content
            onTapped: {
                root.clicked();
                root.toggled();
            }
        }
    }

    Text {
        id: label

        anchors.centerIn: parent
        text: root.text
        color: root._content
        font.pixelSize: Tokens.font_size_label_l

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_short_3
                easing: Motion.standard
            }
        }
    }
}
