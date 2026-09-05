// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The vertical stack of this screen's workspaces. Geometry follows niri's
// workspaces_render_geo: every cell is the screen scaled by zoom, cells are
// separated by a gap of 10% of the screen height (also zoomed), the column
// is centred horizontally, and the CURRENT workspace sits at the screen's
// centre. At progress 0 the current cell therefore covers the real screen
// exactly, so the open animation starts from what the user was looking at.

import QtQuick

Item {
    id: column

    required property Item root

    readonly property real cellWidth: column.root.snap(column.width * column.root.zoom)
    readonly property real cellHeight: column.root.snap(column.height * column.root.zoom)
    readonly property real gap: column.root.snap(column.height * 0.1 * column.root.zoom)
    readonly property real cellX: column.root.snap((column.width - column.cellWidth) / 2)
    // A live per-output desktop swipe (KWin's desktopChanging) slides the
    // column by whole workspaces; a programmatic switch emits none.
    property real swipeOffset: 0
    Connections {
        target: column.root.effect
        function onDesktopOffsetChanged(screen) {
            if (screen === column.root.targetScreen) {
                column.swipeOffset = column.root.effect.desktopOffsetForScreen(screen).y;
            }
        }
    }
    // Held false for one animation duration after a workspace is created or
    // destroyed by a drop (KWin's desktopJustCreated), so the destroy
    // debounce's later renumber cannot lurch the column.
    property bool animateReflow: true
    Timer {
        id: reflowHold
        interval: column.root.effect.animationDuration
        onTriggered: column.animateReflow = true
    }
    function holdReflow() {
        column.animateReflow = false;
        reflowHold.restart();
    }

    function cellY(sliceIndex) {
        const step = column.cellHeight + column.gap;
        return column.root.snap((column.height - column.cellHeight) / 2 + (sliceIndex - column.root.currentIndex - column.swipeOffset) * step);
    }

    Repeater {
        model: column.root.slice
        delegate: WorkspaceCell {
            required property int index
            required property var modelData
            root: column.root
            column: column
            sliceIndex: index
            entry: modelData
            x: column.cellX
            y: column.cellY(index)
            width: column.cellWidth
            height: column.cellHeight
            Behavior on y {
                enabled: column.animateReflow && column.root.organized && !column.root.effect.gestureInProgress
                NumberAnimation {
                    duration: column.root.effect.animationDuration
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    // A screen the map knows but that owns no workspace yet: one
    // non-droppable placeholder where its first workspace will appear.
    WorkspaceCell {
        visible: column.root.screenKnown && column.root.slice.length === 0
        root: column.root
        column: column
        sliceIndex: 0
        placeholder: true
        entry: ({
                id: "",
                index: 0,
                name: ""
            })
        x: column.cellX
        y: column.cellY(0)
        width: column.cellWidth
        height: column.cellHeight
    }
}
