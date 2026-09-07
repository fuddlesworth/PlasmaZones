// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.BarIconButton, an icon-only bar button.
//
// Shared chrome for the bar's trailing buttons (notifications, power): a
// bare glyph on the band with no filled state layer (05 R1). Hover is
// the glyph going to full brightness plus the rail segment the bar lights
// above it; press is a 0.96 scale. A tabular count sits at the top right
// when `badgeCount` is set. Pointer, keyboard and assistive-tech paths
// all route through one activation function. The keyboard path is
// dormant in the shipped bar, whose panel takes no keyboard focus.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    property string iconName: ""
    property string label: ""
    property int badgeCount: 0
    // Resting glyph opacity; the power button rests lower than the rest.
    property real restOpacity: 0.7
    // Rail-axis hue of this chip, bound by the slot that mounts it.
    property real railT: 0.9

    signal activated

    /// Whether this button's panel is the open one. Same stash and same
    /// rule as ChipTrigger: BarController puts the registry id on the
    /// widget, so a button recognises itself without being told its id.
    readonly property bool panelOpen: BarRegistry.openPanelId !== "" && root._barWidgetId === BarRegistry.openPanelId

    implicitWidth: 24
    implicitHeight: 20

    Accessible.role: Accessible.Button
    Accessible.name: root.badgeCount > 0 ? qsTr("%1, %2 unread").arg(root.label).arg(root.badgeCount) : root.label
    Accessible.onPressAction: root._activate()

    activeFocusOnTab: enabled
    Keys.onSpacePressed: event => root._activateFromKey(event)
    Keys.onReturnPressed: event => root._activateFromKey(event)
    Keys.onEnterPressed: event => root._activateFromKey(event)

    function _activate() {
        if (!root.enabled)
            return;
        root.activated();
    }

    function _activateFromKey(event) {
        if (event.isAutoRepeat || !root.enabled)
            return;
        root._activate();
    }

    HoverHandler {
        id: hover

        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap

        enabled: root.enabled
        onTapped: root._activate()
    }

    Kirigami.Icon {
        anchors.centerIn: parent
        width: 16
        height: 16
        source: root.iconName
        isMask: true
        color: Theme.on_surface
        opacity: !root.enabled ? StateLayer.disabled_content : (hover.hovered || root.activeFocus || root.panelOpen ? 1 : root.restOpacity)
        scale: tap.pressed ? 0.96 : 1

        Behavior on opacity {
            NumberAnimation {
                duration: hover.hovered || root.panelOpen ? Motion.duration_enter : Motion.duration_release
                easing: hover.hovered || root.panelOpen ? Motion.enter : Motion.release
            }
        }
        Behavior on scale {
            NumberAnimation {
                duration: Motion.duration_tick
                easing: Motion.reveal
            }
        }
    }

    // Keyboard focus: a white stroke, the one place the button paints an
    // outline.
    SpectrumStroke {
        anchors.fill: parent
        radius: Tokens.radius_edge
        t: root.railT
        focused: root.activeFocus
        visible: root.activeFocus
    }

    TabularText {
        visible: root.badgeCount > 0
        opacity: root.enabled ? 1 : StateLayer.disabled_content
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: -3
        text: root.badgeCount > 99 ? "99+" : root.badgeCount.toString()
        Accessible.ignored: true
        font.pixelSize: 9
        font.weight: Tokens.font_weight_medium
        tickOnChange: true
        t: root.railT
    }
}
