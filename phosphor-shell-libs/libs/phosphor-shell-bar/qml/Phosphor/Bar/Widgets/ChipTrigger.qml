// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.ChipTrigger, the click affordance a status chip overlays on
// itself to become a panel trigger.
//
// The status widgets (network, bluetooth, battery, clock, audio, media)
// are readouts first: their root is a BarWidget carrying the value, and
// the press is a second role layered on top. This type is that layer, so
// the five of them do not each carry their own copy of the same handler
// quad and drift apart on the details that are easy to get subtly wrong
// (the cursor, the press scale, what assistive tech is told).
//
// It deliberately does NOT declare `activated` on the widget's behalf.
// BarController duck-types a trigger by looking for an `activated()`
// signal on the delegate ITSELF, so each widget declares its own and
// relays this one:
//
//     BarWidget {
//         id: root
//         signal activated
//         ChipTrigger {
//             actionName: qsTr("Show network panel")
//             onTriggered: root.activated()
//         }
//     }
//
// Pointer and assistive tech only, no keyboard leg. The bar's panel takes
// no keyboard focus, so there is nothing to Tab from; BarIconButton
// carries the full keyboard quad because it is the shared button atom and
// is meant to work wherever a focused surface hosts it.

import QtQuick

Item {
    id: root

    /// What pressing this does, for assistive tech. A verb phrase, since
    /// it is announced as a button's action and not as its value: the
    /// value is already on the chip's own Accessible node.
    property string actionName: ""

    /// Whether the press is live. A chip whose service has not resolved
    /// shows a readout but has nothing to open.
    property bool active: true

    signal triggered

    // Fills whatever hosts it, so a widget adds one child and is done.
    anchors.fill: parent

    // Hover and press state, for a host that wants to reflect the press
    // (a scale, a brighter glyph). Readonly: the host reads, never writes.
    readonly property bool hovered: hover.hovered
    readonly property bool pressed: tap.pressed

    /// Whether THIS chip's panel is the one currently open.
    ///
    /// BarController stashes each widget's registry id on the widget as
    /// `_barWidgetId` so the activation relay can recover it from sender();
    /// the same stash is what lets a chip recognise itself here without
    /// being told its own id twice. `parent` is the widget root, since a
    /// ChipTrigger is declared as its child.
    readonly property bool panelOpen: parent !== null && BarRegistry.openPanelId !== "" && parent._barWidgetId === BarRegistry.openPanelId

    /// What a host should use for "this chip is lit": open, hovered, or
    /// both. Kept here so the five chips cannot drift on the rule.
    readonly property bool lit: root.panelOpen || root.hovered

    Accessible.role: Accessible.Button
    Accessible.name: root.actionName
    Accessible.onPressAction: root._activate()

    function _activate() {
        if (!root.active)
            return;
        root.triggered();
    }

    HoverHandler {
        id: hover

        enabled: root.active
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap

        enabled: root.active
        onTapped: root._activate()
    }
}
