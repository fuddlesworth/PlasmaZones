// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.DecorationSlot, where a surface pack lands on chrome.
//
// Every chrome surface is a decoration host like a window frame (A1 §2.4),
// but the host that renders a pack chain (the shell's SurfaceDecoration)
// lives above these libraries, in the shell process. So a surface declares
// a slot: it names its frame item (the one a pack wraps, which carries
// `shaderAnchor: true`), its surface path in the decoration tree, and
// whether it is focused; the composition root hands it a Component that
// draws the chain. Without one the slot is empty and the surface draws
// its own stroke; with one, `active` says a chain is engaged so the
// surface can hand its outline to the pack.
//
//   DecorationSlot {
//       anchors.fill: parent
//       component: panel.decoration
//       contentItem: band
//       surfacePath: "shell.phosphor.bar"
//   }
//
// The instantiated item is given `contentItem`, `surfacePath` and
// `focused` when it declares them, through bindings that follow this
// slot's own, so a surface that swaps its frame or loses focus keeps the
// pack in step.

import QtQuick

Item {
    id: slot

    property Component component: null
    property Item contentItem: null
    property string surfacePath: ""
    property bool focused: true

    // A chain is engaged on this surface: the instantiated host reports
    // `decorationActive` (SurfaceDecoration's contract), and a host
    // without that property counts as active while it exists.
    readonly property bool active: loader.item !== null && (loader.item.decorationActive === undefined || loader.item.decorationActive === true)
    readonly property Item item: loader.item

    Loader {
        id: loader

        anchors.fill: parent
        active: slot.component !== null && slot.contentItem !== null
        sourceComponent: slot.component
    }

    Binding {
        target: loader.item
        property: "contentItem"
        value: slot.contentItem
        when: loader.item !== null && loader.item.contentItem !== undefined
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: loader.item
        property: "surfacePath"
        value: slot.surfacePath
        when: loader.item !== null && loader.item.surfacePath !== undefined
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: loader.item
        property: "focused"
        value: slot.focused
        when: loader.item !== null && loader.item.focused !== undefined
        restoreMode: Binding.RestoreBindingOrValue
    }
}
