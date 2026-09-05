// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Slot, one left/center/right bar region.
//
// A horizontal run of bare chips on the band: no chip backgrounds, no
// pills (05 §8). Groups are separated by a 1 × 12 px hairline at 25 %
// white. The slot is registry-agnostic: it is handed a list of groups and
// a `registry` exposing createWidgetFor(id, parent) -> Item (the shell's
// BarController). Each widget is parented under its cell, so the cell's
// destruction cascades through the QObject parent chain; the slot never
// calls destroy(), which is why the factory hands back CppOwnership items.
//
// The slot reports which cell the pointer is over (`hoveredCell`) so the
// bar can light the rail segment above it, and looks cells up by widget
// id (`cellFor`) so a pane can hang under its chip.
//
//   Slot {
//       groups: [["clock"], ["audio", "network", "battery"]]
//       registry: BarRegistry
//   }

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

RowLayout {
    id: root

    // Groups to mount, left to right. Each entry is an array of widget ids
    // sharing one hairline-separated group.
    property var groups: []
    // Provider: an object exposing createWidgetFor(id, parent) -> Item.
    // Must already be set when the slot is created; mounting is one-shot.
    property QtObject registry: null
    // The bar's width, for the rail-axis hue of a chip's underline.
    property real screenWidth: 0
    // The bar's QScreen name, for widgets that key on the output (the
    // placement map). Handed down explicitly: the Window attached
    // property is not a reliable route to the PanelWindow's screen.
    property string screenName: ""

    // The cell under the pointer, or null.
    property Item hoveredCell: null
    // Bumped each time a cell mounts its widget, so a lookup binding
    // re-evaluates once the cells exist.
    property int mountedCount: 0

    // Cell items by widget id.
    property var _cells: ({})

    function cellFor(id) {
        const c = root._cells[id];
        return c ? c : null;
    }

    spacing: Tokens.spacing_m

    Repeater {
        model: root.groups

        delegate: Item {
            id: group

            required property int index
            required property var modelData // array of widget ids

            // A bare string is a one-widget group; a list-like is a group.
            // Duck-typed: a group arrives through a `var` model as a
            // QVariantList, which Array.isArray rejects.
            readonly property var widgetIds: {
                if (typeof group.modelData === "string")
                    return [group.modelData];
                if (group.modelData && group.modelData.length !== undefined)
                    return group.modelData;
                return [];
            }

            Component.onCompleted: {
                if (typeof group.modelData === "string")
                    console.warn("Slot: group entry should be an array of ids, got the bare string", group.modelData);
                else if (!group.modelData || group.modelData.length === undefined)
                    console.warn("Slot: ignoring group entry that is neither a list of ids nor a string:", group.modelData);
            }

            readonly property bool hasContent: inner.implicitWidth > 0
            readonly property bool hasHairline: group.index > 0

            // Collapses to nothing when every widget hides, so no empty
            // group takes up room in the bar. The parent RowLayout skips
            // invisible items, which closes the spacing slot too.
            implicitWidth: group.hasContent ? inner.implicitWidth + (group.hasHairline ? 1 + Tokens.spacing_m : 0) : 0
            implicitHeight: inner.implicitHeight
            visible: group.hasContent
            Layout.alignment: Qt.AlignVCenter

            // Hairline before every group but the first.
            Rectangle {
                id: hairline

                visible: group.hasHairline
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: 12
                color: Theme.on_surface
                opacity: 0.25
            }

            RowLayout {
                id: inner

                anchors.left: parent.left
                anchors.leftMargin: group.hasHairline ? 1 + Tokens.spacing_m : 0
                anchors.verticalCenter: parent.verticalCenter
                spacing: Tokens.spacing_s

                Repeater {
                    model: group.widgetIds

                    delegate: Item {
                        id: cell

                        required property string modelData
                        property Item widget: null

                        readonly property bool shown: cell.widget !== null && cell.widget.implicitWidth > 0
                        // Rail-axis hue of this chip, sampled at its centre.
                        readonly property real railT: {
                            void root.width;
                            void root.x;
                            const p = cell.mapToItem(null, cell.width / 2, 0);
                            return Spectrum.tForX(p.x, root.screenWidth);
                        }

                        implicitWidth: cell.shown ? cell.widget.implicitWidth : 0
                        implicitHeight: cell.widget ? cell.widget.implicitHeight : 0
                        visible: cell.shown
                        Layout.alignment: Qt.AlignVCenter

                        HoverHandler {
                            id: hover

                            onHoveredChanged: {
                                if (hovered)
                                    root.hoveredCell = cell;
                                else if (root.hoveredCell === cell)
                                    root.hoveredCell = null;
                            }
                        }

                        // Hover: a 1 px underline in the rail's hue at this x.
                        // Chips never paint a hover background.
                        SpectrumUnderline {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: -2
                            height: 1
                            length: cell.width
                            t: cell.railT
                            restOpacity: 0
                            opacity: hover.hovered ? 1 : 0
                            Behavior on opacity {
                                NumberAnimation {
                                    duration: hover.hovered ? Motion.duration_enter : Motion.duration_release
                                    easing: hover.hovered ? Motion.enter : Motion.release
                                }
                            }
                        }

                        Component.onCompleted: {
                            if (root.registry && typeof root.registry.createWidgetFor === "function")
                                cell.widget = root.registry.createWidgetFor(cell.modelData, cell);
                            else
                                console.warn("Slot: no registry provided; cannot mount", cell.modelData);
                            if (cell.widget) {
                                // The factory only parents the widget; the
                                // cell sizes it from the widget's implicit
                                // size, and binds the widget's actual size
                                // back so root-anchored handlers get a hit
                                // area.
                                cell.widget.width = Qt.binding(() => cell.width);
                                cell.widget.height = Qt.binding(() => cell.height);
                                // Widgets that draw a rail-axis colour read
                                // the chip's hue from their parent cell.
                                if (cell.widget.railT !== undefined)
                                    cell.widget.railT = Qt.binding(() => cell.railT);
                                if (cell.widget.screenName !== undefined)
                                    cell.widget.screenName = Qt.binding(() => root.screenName);
                            }
                            const cells = root._cells;
                            cells[cell.modelData] = cell;
                            root._cells = cells;
                            root.mountedCount += 1;
                        }
                    }
                }
            }
        }
    }
}
