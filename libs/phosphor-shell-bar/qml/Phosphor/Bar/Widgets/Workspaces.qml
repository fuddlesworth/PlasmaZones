// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Workspaces, the workspace pager.
//
// One pill per workspace, the current one widened and filled. Clicking a
// pill switches to it. Collapses to zero width on a compositor with no
// workspace source, where a pager would otherwise show a single permanent
// pill that does nothing.
//
// Addressed by workspace ID throughout, never by row: positions renumber
// whenever a workspace is created or removed, so a delegate that captured
// its index would switch to the wrong workspace after any such change.

import QtQuick
// QUALIFIED, unlike the other widgets. This file's own type is
// Phosphor.Bar.Workspaces and the singleton it reads is
// Phosphor.Shell.Workspaces; an unqualified import would leave `Workspaces`
// ambiguous between the two, which resolves silently rather than erroring.
import Phosphor.Shell as Shell
import Phosphor.Theme

BarWidget {
    id: root

    // Dim factor for an inactive pill, named rather than repeated inline;
    // Tray names the same value for the same reason.
    readonly property real passiveOpacity: 0.5

    // `count` alone, deliberately not `supported`. `count` is 0 whenever
    // there is no workspace source, so it already covers that case — and
    // unlike `supported`, which is CONSTANT and latches at whatever the
    // very first D-Bus probe saw, it is notified. Gating on `supported`
    // would hide the pager for the entire session if the shell happened to
    // reach the singleton before KWin registered on the bus.
    available: Shell.Workspaces.count > 0
    contentWidth: row.implicitWidth
    contentHeight: row.implicitHeight

    Accessible.role: Accessible.PageTabList
    Accessible.name: "Workspaces"

    Row {
        id: row

        spacing: Tokens.spacing_xs

        Repeater {
            model: Shell.Workspaces.model

            // The cell is the hit target and the pill is only what gets
            // painted: an 8px dot is far too small to click comfortably, so
            // the cell gives it vertical padding. The height is a fixed
            // comfortable target rather than the bar's full height, which
            // would make the pills' hit areas meet with no gap between
            // neighbours.
            delegate: Item {
                id: cell

                required property string workspaceId
                required property string name
                required property bool isActive

                width: pill.width
                height: 24

                Accessible.role: Accessible.PageTab
                // The name, not the position: the label is what the user
                // recognises, and it is what they renamed it to.
                Accessible.name: cell.name
                Accessible.selected: cell.isActive
                Accessible.onPressAction: Shell.Workspaces.switchTo(cell.workspaceId)

                Rectangle {
                    id: pill

                    // The active workspace is a wide lozenge, the rest are
                    // dots. Equal heights so the row never reflows.
                    width: cell.isActive ? 22 : 8
                    height: 8
                    radius: height / 2
                    anchors.centerIn: parent
                    color: cell.isActive ? Theme.primary : Theme.on_surface_variant
                    opacity: cell.isActive ? 1.0 : root.passiveOpacity

                    Behavior on width {
                        NumberAnimation {
                            duration: Motion.duration_short_3
                            easing: Motion.standard
                        }
                    }
                    // Same curve as the width above: the two animate
                    // together on every switch, and leaving this one on the
                    // implicit Linear made the pair visibly disagree.
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Motion.duration_short_3
                            easing: Motion.standard
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Shell.Workspaces.switchTo(cell.workspaceId)
                }
            }
        }
    }
}
