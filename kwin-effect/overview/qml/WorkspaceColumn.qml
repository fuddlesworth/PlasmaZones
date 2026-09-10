// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The vertical stack of this screen's workspaces. Geometry follows niri's
// workspaces_render_geo: every cell is the screen scaled by zoom, cells are
// separated by a gap of 10% of the screen height (also zoomed), the column
// is centred horizontally, and the ANCHOR workspace sits at the screen's
// centre. The anchor is the current workspace, except during the close
// sequence after a click, when it is the chosen cell so the zoom-in lands
// on it before the compositor's current-desktop change arrives. At
// progress 0 the anchor cell therefore covers the real screen exactly.
//
// Every gap, plus the space above the first cell and below the last, is a
// WorkspaceGapDropArea whose index is the slice index a workspace created
// there would take.

import QtQuick

Item {
    // Not "column": the cell delegates below declare a property of that
    // name, and inside a delegate the property shadows the id.
    id: stack

    required property Item root

    readonly property real cellWidth: stack.root.snap(stack.width * stack.root.zoom)
    readonly property real cellHeight: stack.root.snap(stack.height * stack.root.zoom)
    readonly property real gap: stack.root.snap(stack.height * 0.1 * stack.root.zoom)
    readonly property real cellX: stack.root.snap((stack.width - stack.cellWidth) / 2)
    // The slice index the column is centred on. anchorOverride is set by
    // the close sequence and cleared by the root once the effect is closed.
    property int anchorOverride: -1
    readonly property int anchorIndex: stack.anchorOverride >= 0 ? stack.anchorOverride : stack.root.currentIndex
    // A live per-output desktop swipe (KWin's desktopChanging) slides the
    // column by whole workspaces; a programmatic switch emits none.
    property real swipeOffset: 0
    Connections {
        target: stack.root.effect
        function onDesktopOffsetChanged(screen) {
            if (screen === stack.root.targetScreen) {
                stack.swipeOffset = stack.root.effect.desktopOffsetForScreen(screen).y;
            }
        }
    }
    // Held false for one animation duration after a workspace is created or
    // destroyed by a drop (KWin's desktopJustCreated), so the destroy
    // debounce's later renumber cannot lurch the stack.
    property bool animateReflow: true
    Timer {
        id: reflowHold
        interval: stack.root.effect.animationDuration
        onTriggered: stack.animateReflow = true
    }
    function holdReflow() {
        stack.animateReflow = false;
        reflowHold.restart();
    }

    function anchorTo(sliceIndex) {
        stack.anchorOverride = sliceIndex;
    }

    function cellY(sliceIndex) {
        const step = stack.cellHeight + stack.gap;
        return stack.root.snap((stack.height - stack.cellHeight) / 2 + (sliceIndex - stack.anchorIndex - stack.swipeOffset) * step);
    }

    // The cell item for a slice index, or null.
    function cellAt(sliceIndex) {
        return cells.itemAt(sliceIndex);
    }

    // What lies under a point in this column's coordinates: {cell} for a
    // workspace, {gapIndex} for a gap, or null beside the column.
    function targetAt(point) {
        if (point.x < stack.cellX || point.x >= stack.cellX + stack.cellWidth) {
            return null;
        }
        const count = stack.root.slice.length;
        for (let i = 0; i < count; ++i) {
            const top = stack.cellY(i);
            if (point.y < top) {
                return point.y >= top - stack.gap ? ({
                        gapIndex: i
                    }) : null;
            }
            if (point.y < top + stack.cellHeight) {
                const cell = stack.cellAt(i);
                return cell ? ({
                        cell: cell
                    }) : null;
            }
        }
        if (count > 0 && point.y < stack.cellY(count - 1) + stack.cellHeight + stack.gap) {
            return ({
                    gapIndex: count
                });
        }
        return null;
    }

    Repeater {
        id: gaps
        model: stack.root.slice.length + 1
        delegate: WorkspaceGapDropArea {
            required property int index
            root: stack.root
            gapIndex: index
            x: stack.cellX
            y: stack.cellY(index) - stack.gap
            width: stack.cellWidth
            height: stack.gap
        }
    }

    Repeater {
        id: cells
        model: stack.root.slice
        delegate: WorkspaceCell {
            required property int index
            required property var modelData
            root: stack.root
            column: stack
            sliceIndex: index
            entry: modelData
            x: stack.cellX
            y: stack.cellY(index)
            width: stack.cellWidth
            height: stack.cellHeight
            Behavior on y {
                enabled: stack.animateReflow && stack.root.organized && !stack.root.effect.gestureInProgress
                NumberAnimation {
                    duration: stack.root.effect.animationDuration
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    // A screen the map knows but that owns no workspace yet: one
    // non-droppable placeholder where its first workspace will appear.
    WorkspaceCell {
        visible: stack.root.screenKnown && stack.root.slice.length === 0
        root: stack.root
        column: stack
        sliceIndex: 0
        placeholder: true
        entry: ({
                id: "",
                index: 0,
                name: ""
            })
        x: stack.cellX
        y: stack.cellY(0)
        width: stack.cellWidth
        height: stack.cellHeight
    }
}
