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
    // Injected by BarHost (the shell's BarController, a QObject). Null in
    // isolation, in which case the slot mounts nothing.
    //
    // Mounting is one-shot per cell, so this must already be set when the
    // slot is created; assigning it later mounts nothing. The shell sets the
    // BarRegistry context property before engine.load(), which satisfies
    // that.
    property QtObject registry: null

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

            // Normalise the host-supplied entry. `groups` is config data and
            // a layout editor will write it, so a bare string (["clock"]
            // instead of [["clock"]]) is a plausible mistake; treat it as a
            // one-widget group rather than letting Repeater iterate the
            // string's characters.
            // Kept PURE: a binding can re-evaluate any number of times, so
            // diagnosing a malformed entry in here would re-warn on each one.
            // The warning lives in Component.onCompleted below instead.
            //
            // A bare string is handled first because strings also carry
            // `length`, so the list check would otherwise walk their
            // characters. The list check duck-types rather than using
            // Array.isArray: a group arrives through a `var` model as a
            // QVariantList, which is list-like but not a native JS Array, so
            // Array.isArray rejects every real group.
            readonly property var widgetIds: {
                if (typeof chip.modelData === "string")
                    return [chip.modelData];
                if (chip.modelData && chip.modelData.length !== undefined)
                    return chip.modelData;
                return [];
            }

            // One diagnostic per chip, not one per binding evaluation.
            Component.onCompleted: {
                if (typeof chip.modelData === "string")
                    console.warn("Slot: group entry should be an array of ids, got the bare string", chip.modelData);
                else if (!chip.modelData || chip.modelData.length === undefined)
                    console.warn("Slot: ignoring group entry that is neither a list of ids nor a string:", chip.modelData);
            }

            readonly property bool hasContent: inner.implicitWidth > 0

            implicitWidth: chip.hasContent ? inner.implicitWidth + Tokens.spacing_m * 2 : 0
            // Every chip rests at the same pill height, so a row of them
            // reads as one band. Adding padding to the content height here
            // would instead give each chip a different height (and, with
            // radius: height/2, a different corner) according to whatever
            // it happens to hold. Only a widget genuinely taller than the
            // pill grows its chip; nothing in the catalogue does today.
            implicitHeight: Math.max(30, inner.implicitHeight)
            visible: chip.hasContent
            radius: height / 2
            color: Theme.surface_variant
            Layout.alignment: Qt.AlignVCenter

            RowLayout {
                id: inner

                anchors.centerIn: parent
                spacing: Tokens.spacing_s

                Repeater {
                    model: chip.widgetIds

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
