// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.Tile, the shared chrome for one toggle rail.
//
// A control is a 52 px RAIL (A3 §2b): glyph, label, the live readout
// right-aligned in tabular figures, and a 2 px spectrum underline along
// the bottom that IS the control's state light: full-width blue when on,
// 25 % on_surface when off, purple while pending. No filled tile, no
// border, no button. The concrete tiles (NetworkTile, AudioTile, ...)
// fill in `iconName`, `label`, `sublabel`, `active`, and whatever detail
// content they carry.
//
//   Tile {
//       NetworkHost { id: host }
//       iconName: "network-wireless"
//       label: qsTr("Wi-Fi")
//       sublabel: host.connectivity === NetworkHost.Full ? qsTr("Connected") : qsTr("Not connected")
//       active: host.wirelessEnabled
//       onToggled: host.wirelessEnabled = !host.wirelessEnabled
//   }
//
// Tiles are deliberately NOT self-latching: `toggled` asks the service to
// change, and `active` follows when the service echoes the new state
// back. A tile that latched locally would show a state the system never
// reached whenever a request failed or was overridden elsewhere.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    property string iconName: ""
    property string label: ""
    property string sublabel: ""
    property bool active: false
    // A request is in flight and the service has not echoed yet: the
    // underline goes purple (the state axis' "pending").
    property bool pending: false
    property bool available: true
    property string detailTitle: ""
    property Component detailContent: null
    property bool detailEnabled: true
    readonly property bool hasDetail: root.detailEnabled && root.detailContent !== null
    // Layout hint read by ControlCenter. Every rail spans the pane.
    readonly property bool spansRow: true

    signal toggled
    signal detailRequested

    implicitWidth: 320
    implicitHeight: Math.max(52, content.implicitHeight + Tokens.spacing_m)
    opacity: root.available ? 1 : StateLayer.disabled_content

    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.description: root.available ? root.sublabel : (root.sublabel.length > 0 ? qsTr("%1 — unavailable").arg(root.sublabel) : qsTr("Unavailable"))
    Accessible.onPressAction: root._activate()

    Keys.onSpacePressed: event => root._activateFromKey(event)
    Keys.onReturnPressed: event => root._activateFromKey(event)
    Keys.onEnterPressed: event => root._activateFromKey(event)

    function _activate(): void {
        if (root.available)
            root.toggled();
    }

    function _activateFromKey(event: var): void {
        if (event.isAutoRepeat)
            return;
        if (!root.available)
            return;
        root.toggled();
        event.accepted = true;
    }

    HoverHandler {
        id: hover

        enabled: root.available
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: root.available
        onTapped: root.toggled()
    }

    RowLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Tokens.spacing_xs
        anchors.rightMargin: root.hasDetail ? chevron.width + Tokens.spacing_s : Tokens.spacing_xs
        spacing: Tokens.spacing_m

        Kirigami.Icon {
            source: root.iconName
            isMask: true
            color: Theme.on_surface
            opacity: root.active ? 1 : 0.7
            implicitWidth: 16
            implicitHeight: 16
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            Accessible.ignored: true
            text: root.label
            color: Theme.on_surface
            font.family: Tokens.font_family_ui
            font.pixelSize: Tokens.font_size_body_l
            font.weight: Tokens.font_weight_medium
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        TabularText {
            Accessible.ignored: true
            text: root.sublabel
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_m
            elide: Text.ElideRight
            visible: root.sublabel !== ""
            Layout.maximumWidth: root.width * 0.45
        }
    }

    // The state light: the 2 px underline IS the toggle.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        color: root.pending ? Spectrum.pending : (root.active ? Spectrum.active : Theme.on_surface)
        opacity: root.active || root.pending ? (hover.hovered ? 1 : 0.9) : (hover.hovered ? 0.45 : 0.25)

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }
        Behavior on opacity {
            NumberAnimation {
                duration: hover.hovered ? Motion.duration_enter : Motion.duration_release
                easing: hover.hovered ? Motion.enter : Motion.release
            }
        }
    }

    // Keyboard focus: a white stroke, the one place a rail paints an
    // outline.
    SpectrumStroke {
        anchors.fill: parent
        radius: Tokens.radius_edge
        focused: root.activeFocus
        // `visible` is what keeps the resting spectrum stroke off an unfocused
        // rail. It must not be paired with `opacity: 0`, which would multiply
        // the focus ring away too and leave this painting nothing at all.
        visible: root.activeFocus
    }

    Item {
        id: chevron

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28
        visible: root.hasDetail

        Kirigami.Icon {
            anchors.centerIn: parent
            width: 16
            height: 16
            source: root.LayoutMirroring.enabled ? "go-previous-symbolic" : "go-next-symbolic"
            isMask: true
            color: Theme.on_surface
            opacity: 0.7
        }

        HoverHandler {
            enabled: root.available && root.hasDetail
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            enabled: root.available && root.hasDetail
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: root.detailRequested()
        }
    }
}
