// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Slot, one left/center/right bar region.
//
// A horizontal run of island chips. The slot is registry-agnostic: it is
// handed a list of groups and a `registry` object exposing
// createWidgetFor(id, parent) -> Item (the shell's BarController). Each
// group is an array of widget ids that share one rounded chip, so related
// widgets (the status icons, the trailing buttons) read as a single
// island while others keep their own. Each widget is parented under its
// cell, so the cell's destruction cascades through the QObject parent
// chain and reclaims the widget; the slot never calls destroy(), which is
// why the factory hands back CppOwnership items.
//
//   Slot {
//       groups: [["clock"], ["audio", "network", "battery"]]
//       registry: BarRegistry
//   }

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme

RowLayout {
    id: root

    // Groups to mount, left to right. Each entry is an array of widget ids
    // sharing one chip; a single-id array is a lone-widget chip.
    property var groups: []
    // Provider: an object exposing createWidgetFor(id, parent) -> Item.
    // Injected by BarHost (the shell's BarController). Null in isolation,
    // in which case the slot mounts nothing.
    property var registry: null

    spacing: Tokens.spacing_s

    Repeater {
        model: root.groups

        // One rounded island chip per group. The slate surface_variant is
        // clearly lighter than the navy capsule, so the chip reads as a
        // distinct island. A chip whose every widget hides itself (all
        // zero-width) collapses so no empty pill shows.
        delegate: Rectangle {
            id: chip

            required property var modelData // array of widget ids for this chip

            readonly property bool hasContent: inner.implicitWidth > 0

            implicitWidth: chip.hasContent ? inner.implicitWidth + Tokens.spacing_m * 2 : 0
            implicitHeight: 30
            visible: chip.hasContent
            radius: height / 2
            color: Theme.surface_variant
            Layout.alignment: Qt.AlignVCenter

            RowLayout {
                id: inner

                anchors.centerIn: parent
                spacing: Tokens.spacing_s

                Repeater {
                    model: chip.modelData

                    delegate: Item {
                        id: cell

                        required property string modelData
                        // The mounted widget. A typed Item property so QML
                        // nulls it automatically if the widget is destroyed.
                        property Item widget: null

                        // A hidden widget (zero implicit width) collapses its
                        // cell; QtQuick.Layouts drops invisible items, so the
                        // chip closes the gap and, if every cell is empty,
                        // inner.implicitWidth is 0 and the chip hides.
                        readonly property bool shown: cell.widget !== null && cell.widget.implicitWidth > 0

                        implicitWidth: cell.shown ? cell.widget.implicitWidth : 0
                        implicitHeight: cell.widget ? cell.widget.implicitHeight : 0
                        visible: cell.shown
                        Layout.alignment: Qt.AlignVCenter

                        Component.onCompleted: {
                            if (root.registry && typeof root.registry.createWidgetFor === "function")
                                cell.widget = root.registry.createWidgetFor(cell.modelData, cell);
                            else
                                console.warn("Slot: no registry provided; cannot mount", cell.modelData);
                        }
                    }
                }
            }
        }
    }
}
