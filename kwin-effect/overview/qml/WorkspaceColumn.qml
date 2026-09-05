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
    // Not "column": the cell delegates below declare a property of that
    // name, and inside a delegate the property shadows the id.
    id: stack

    required property Item root

    readonly property real cellWidth: stack.root.snap(stack.width * stack.root.zoom)
    readonly property real cellHeight: stack.root.snap(stack.height * stack.root.zoom)
    readonly property real gap: stack.root.snap(stack.height * 0.1 * stack.root.zoom)
    readonly property real cellX: stack.root.snap((stack.width - stack.cellWidth) / 2)
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

    function cellY(sliceIndex) {
        const step = stack.cellHeight + stack.gap;
        return stack.root.snap((stack.height - stack.cellHeight) / 2 + (sliceIndex - stack.root.currentIndex - stack.swipeOffset) * step);
    }

    Repeater {
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
