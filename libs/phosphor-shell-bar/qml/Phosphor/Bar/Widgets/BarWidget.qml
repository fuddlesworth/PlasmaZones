// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.BarWidget, the base for a bar widget that collapses when it
// has nothing to show.
//
// Every status widget in the bar needs the same three derived properties,
// and getting them wrong has a specific, severe failure mode that this
// type exists to make unreachable.
//
// `Item.visible` READS as EFFECTIVE visibility — it is false whenever any
// ancestor is hidden, not merely when this item is. Slot gates each chip's
// cell on the widget's `implicitWidth`. So a widget that derives its width
// from `visible` closes a loop: hidden ancestor makes visible false, which
// makes implicitWidth zero, which makes the slot collapse the cell, which
// keeps visible false. It latches at zero and never recovers, and it does
// so only under an ancestor-visibility ordering that is easy to miss in
// review and in a test.
//
// Subclasses therefore never write `visible` or `implicitWidth`. They set
// `available` (a host-derived predicate — has the service resolved, is
// there a value) and `contentWidth` / `contentHeight` (their content's
// implicit size), and this type does the rest, deriving everything from
// `available` and never from `visible`.
//
//     BarWidget {
//         id: root
//         available: SomeHost.thing !== null
//         contentWidth: row.implicitWidth
//         contentHeight: row.implicitHeight
//         Row { id: row; ... }
//     }

import QtQuick

Item {
    id: root

    /// Whether the widget has something to show. Derive from the host /
    /// service state, never from `visible`.
    property bool available: true

    /// The content's implicit size. Kept separate from implicitWidth /
    /// implicitHeight so the collapse arithmetic stays in one place.
    property real contentWidth: 0
    property real contentHeight: 0

    visible: root.available
    // Collapses the cell when unavailable. Reads `available`, NOT `visible`.
    implicitWidth: root.available ? root.contentWidth : 0
    // Height is unconditional: a zero-width cell already occupies nothing
    // horizontally, and keeping the height stable stops the row's baseline
    // jumping as widgets come and go.
    implicitHeight: root.contentHeight
}
