// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The overview's per-screen root. One instance per output, created by
// QuickSceneEffect with `screenId` (the daemon's canonical id for this
// output) as an initial property. Everything on screen derives from two
// daemon payloads the effect exposes: the workspace map (which workspaces
// this screen owns, in slice order) and the overview model (where each
// workspace's windows are). The QML never moves a window itself; every
// mutation is a verb to the daemon and the next model repositions it.
//
// Interaction lives here too: the drop rule every DropArea and the
// cross-screen receiver share, the close sequence a click or Return runs,
// the wheel, and the keyboard table. Qt Quick DropAreas only see drags from
// their own view, so a drop on another screen arrives through the effect's
// itemDroppedOutOfScreen signal and this root resolves the cell or gap
// under the global point itself.
//
// i18n: this file runs inside KWin's QML engine, whose default catalogue is
// kwin, so every string goes through i18nd("plasmazones", ...) here.

import QtQuick
import org.kde.kwin as KWinComponents

FocusScope {
    id: root
    focus: true

    required property string screenId

    readonly property QtObject effect: KWinComponents.SceneView.effect
    readonly property QtObject targetScreen: KWinComponents.SceneView.screen
    readonly property QtObject currentDesktop: KWinComponents.SceneView.currentDesktop

    // Gate the progress binding until the component is complete so the open
    // animation always runs from 0, even when the effect starts at factor 1
    // (a shortcut toggle).
    property bool organized: false

    // The animated open progress. The effect's partialActivationFactor is
    // the raw gesture value during a swipe and a 0/1 step on toggle; this
    // value follows it directly while a gesture is in progress and animates
    // toward it otherwise. A toggle mid-flight retargets the animation.
    property real progress: 0
    Behavior on progress {
        enabled: root.organized && !root.effect.gestureInProgress
        NumberAnimation {
            duration: root.effect.animationDuration
            easing.type: Easing.OutCubic
        }
    }
    Binding {
        target: root
        property: "progress"
        value: root.organized ? root.effect.partialActivationFactor : 0
    }
    onProgressChanged: if (root.progress === 0) {
        column.anchorOverride = -1;
    }

    // niri: zoom = 1 - p * (1 - zoomSetting).
    readonly property real zoom: 1 - root.progress * (1 - root.effect.zoom)
    readonly property real outputScale: root.targetScreen ? root.targetScreen.devicePixelRatio : 1

    // Cells take input only when fully open, or while a drag started when
    // it was open is still in flight.
    property bool dragActive: false
    readonly property bool interactive: root.progress >= 1 || root.dragActive
    readonly property Item dragProxy: proxy
    readonly property Item workspaceColumn: column

    // Keyboard selection: the highlighted workspace (defaults to the current
    // one, clamped to the slice) and the highlighted window inside it, or
    // none when the workspace itself is highlighted.
    property int selectedSliceOverride: -1
    readonly property int selectedSlice: {
        const last = root.slice.length - 1;
        if (root.selectedSliceOverride < 0 || root.selectedSliceOverride > last) {
            return Math.min(root.currentIndex, Math.max(last, 0));
        }
        return root.selectedSliceOverride;
    }
    property string selectedWindowId: ""
    // Set by a WorkspaceLabel while its inline rename field has focus.
    property bool renaming: false

    // This screen's slice of the workspace map, in slice order.
    readonly property var slice: {
        const slices = root.effect.workspaceMap.slices;
        return (slices && slices[root.screenId]) ? slices[root.screenId] : [];
    }
    // Whether the map knows this screen at all (a known screen with no
    // workspace yet renders one placeholder cell).
    readonly property bool screenKnown: {
        const order = root.effect.workspaceMap.screenOrder;
        return Array.isArray(order) && order.indexOf(root.screenId) !== -1;
    }
    // This screen's overview model: {logicalSize, workspaces: {desktopId: {...}}}.
    readonly property var screenModel: {
        const screens = root.effect.overviewModel.screens;
        return (screens && screens[root.screenId]) ? screens[root.screenId] : null;
    }
    // Slice index of the workspace this output currently shows, resolved by
    // live desktop number (never by list position). The map's `current`
    // flag is not read: the compositor is the authority on what is shown.
    readonly property int currentIndex: {
        const number = root.currentDesktop ? root.currentDesktop.x11DesktopNumber : 0;
        for (let i = 0; i < root.slice.length; ++i) {
            if (root.slice[i].index === number) {
                return i;
            }
        }
        return 0;
    }

    // Round a logical coordinate to the output's physical pixel grid AFTER
    // zoom (niri #1467: rounding before zoom leaves 1px seams).
    function snap(value) {
        return Math.round(value * root.outputScale) / root.outputScale;
    }

    // The single drop rule, shared by the in-view DropAreas and the
    // cross-screen receiver. Returns {method, args} or null for a no-op.
    // A window dropped onto the cell it came from is a no-op: the tile
    // springs back and no verb is sent. A workspace label dropped onto its
    // own cell is a no-op too. Coordinates are cell-local, still zoomed.
    function resolveDrop(payload, targetScreenId, target, localPoint) {
        if (!payload || !target) {
            return null;
        }
        if (target.cell && !target.cell.droppable) {
            return null;
        }
        const x = Math.round(localPoint.x / root.zoom);
        const y = Math.round(localPoint.y / root.zoom);
        if (payload.kind === "pz-window") {
            if (target.cell) {
                if (payload.screenId === targetScreenId && payload.desktopId === target.cell.desktopId) {
                    return null;
                }
                return ({
                        method: "moveWindowToWorkspace",
                        args: [payload.windowId, targetScreenId, target.cell.desktopId, x, y]
                    });
            }
            return ({
                    method: "moveWindowToNewWorkspace",
                    args: [payload.windowId, targetScreenId, target.gapIndex, 0, 0]
                });
        }
        if (payload.kind === "pz-workspace") {
            const index = target.cell ? target.cell.sliceIndex : target.gapIndex;
            if (payload.screenId === targetScreenId) {
                if (target.cell && target.cell.desktopId === payload.desktopId) {
                    return null;
                }
                return ({
                        method: "reorderWorkspace",
                        args: [targetScreenId, payload.desktopId, index]
                    });
            }
            return ({
                    method: "moveWorkspaceToScreen",
                    args: [payload.desktopId, targetScreenId, index]
                });
        }
        return null;
    }

    function sendDrop(payload, target, localPoint) {
        const verb = root.resolveDrop(payload, root.screenId, target, localPoint);
        if (!verb) {
            return;
        }
        root.effect[verb.method].apply(root.effect, verb.args);
        // A new workspace renumbers the column shortly after: hold the
        // reflow so it does not lurch. The source column's hold for a cell
        // emptied by the move is the DragProxy's job, since the source may
        // be another screen's view.
        if (verb.method === "moveWindowToNewWorkspace") {
            column.holdReflow();
        }
    }

    function dropOntoCell(payload, cell, localPoint) {
        root.sendDrop(payload, ({
                cell: cell
            }), localPoint);
    }

    function dropIntoGap(payload, gapIndex) {
        root.sendDrop(payload, ({
                gapIndex: gapIndex
            }), Qt.point(0, 0));
    }

    // A drag from another screen's view released over this one. pos is
    // global; the payload rides on the proxy item that was dragged.
    Connections {
        target: root.effect
        function onItemDroppedOutOfScreen(pos, item, screen) {
            if (screen !== root.targetScreen || !item || !item.payload) {
                return;
            }
            const local = root.targetScreen.mapFromGlobal(pos);
            const inColumn = root.mapToItem(column, local.x, local.y);
            const target = column.targetAt(inColumn);
            if (!target) {
                return;
            }
            const cellPoint = target.cell ? column.mapToItem(target.cell, inColumn.x, inColumn.y) : Qt.point(0, 0);
            root.sendDrop(item.payload, target, cellPoint);
        }
    }

    // The close sequence for a chosen window: the column anchors on its cell
    // so the zoom-in lands there, the daemon switches that screen, the
    // compositor activates the window, and the effect closes.
    function activateWindowIn(cell, windowId) {
        if (!cell || cell.placeholder) {
            return;
        }
        column.anchorTo(cell.sliceIndex);
        root.effect.focusWorkspace(root.screenId, cell.desktopId);
        root.effect.activateWindow(windowId);
        root.effect.deactivate();
    }

    // The close sequence for a chosen workspace: switch and close, keyboard
    // focus unchanged.
    function focusCellAndClose(cell) {
        if (!cell || cell.placeholder) {
            return;
        }
        column.anchorTo(cell.sliceIndex);
        root.effect.focusWorkspace(root.screenId, cell.desktopId);
        root.effect.deactivate();
    }

    // The cell nearest a point in root coordinates, by vertical distance,
    // so a wheel over the backdrop or a dock acts on the closest workspace.
    function nearestCell(point) {
        let best = null;
        let bestDistance = Infinity;
        for (let i = 0; i < root.slice.length; ++i) {
            const cell = column.cellAt(i);
            if (!cell) {
                continue;
            }
            const top = cell.y;
            const bottom = cell.y + cell.height;
            const distance = point.y < top ? top - point.y : (point.y >= bottom ? point.y - bottom : 0);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = cell;
            }
        }
        return best;
    }

    // Switch this screen to the slice entry `steps` away from a cell, no
    // wrap, keyboard focus untouched.
    function focusNeighbour(cell, steps) {
        const index = cell.sliceIndex + steps;
        if (index < 0 || index >= root.slice.length) {
            return;
        }
        root.effect.focusWorkspace(root.screenId, root.slice[index].id);
    }

    // The view texture has no alpha: paint the backdrop over the whole
    // output first.
    Rectangle {
        anchors.fill: parent
        color: root.effect.backdropColor
    }

    // One wallpaper per SCREEN, dimmed as the overview opens. Plasma's
    // desktop window is on every desktop of an output, so a per-cell
    // wallpaper would be N identical full-resolution renders. outputName is
    // the connector name, the one id that is NOT the canonical screen id.
    KWinComponents.DesktopBackground {
        id: wallpaper
        anchors.fill: parent
        outputName: root.targetScreen ? root.targetScreen.name : ""
        // Never empty: with an empty activity the item asks KWin's activities
        // manager for the current one, and a session without activities (a
        // nested kwin_wayland --no-kactivities) has no manager to ask, which
        // crashes the compositor. A window on every activity still matches
        // the sentinel; a real session always has a current activity anyway.
        activity: KWinComponents.Workspace.currentActivity || "00000000-0000-0000-0000-000000000000"
        desktop: root.currentDesktop
        opacity: 1 - 0.55 * root.progress
    }

    WorkspaceColumn {
        id: column
        anchors.fill: parent
        root: root
    }

    DockLayer {
        anchors.fill: parent
        root: root
        opacity: 1 - root.progress
        visible: opacity > 0
    }

    DragProxy {
        id: proxy
        root: root
    }

    // Wheel: vertical steps switch the workspace under (or nearest to) the
    // pointer on THIS screen, one step per 120 units, no wrap; horizontal
    // or Shift+vertical pans a scrolling workspace's strip.
    WheelHandler {
        id: wheel
        enabled: root.interactive
        property real vertical: 0
        property real horizontal: 0
        onWheel: event => {
            const cell = root.nearestCell(Qt.point(event.x, event.y));
            if (!cell) {
                return;
            }
            const shifted = (event.modifiers & Qt.ShiftModifier) !== 0;
            const panDelta = shifted ? event.angleDelta.y : event.angleDelta.x;
            if (panDelta !== 0) {
                if (cell.mode === "scrolling") {
                    wheel.horizontal += panDelta;
                    const steps = Math.trunc(wheel.horizontal / 120);
                    if (steps !== 0) {
                        wheel.horizontal -= steps * 120;
                        // One notch pans a tenth of the workspace width.
                        root.effect.panStrip(root.screenId, cell.desktopId, Math.round(-steps * root.width * 0.1));
                    }
                }
                return;
            }
            if (!root.effect.wheelSwitchesWorkspaces) {
                return;
            }
            wheel.vertical += event.angleDelta.y;
            const steps = Math.trunc(wheel.vertical / 120);
            if (steps !== 0) {
                wheel.vertical -= steps * 120;
                // Wheel up (positive) goes to the previous entry.
                root.focusNeighbour(cell, -steps);
            }
        }
    }

    // Windows of a cell sorted top-to-bottom, left-to-right for keyboard
    // navigation.
    function orderedWindows(cell) {
        const list = cell.windows.slice();
        list.sort((a, b) => (a.rect.y - b.rect.y) || (a.rect.x - b.rect.x));
        return list;
    }

    // The nearest window from the selected one in a direction, or null.
    function neighbourWindow(cell, direction) {
        const list = cell.windows;
        let origin = null;
        for (const w of list) {
            if (w.id === root.selectedWindowId) {
                origin = w;
            }
        }
        if (!origin) {
            return null;
        }
        const cx = origin.rect.x + origin.rect.w / 2;
        const cy = origin.rect.y + origin.rect.h / 2;
        let best = null;
        let bestDistance = Infinity;
        for (const w of list) {
            if (w.id === origin.id) {
                continue;
            }
            const wx = w.rect.x + w.rect.w / 2;
            const wy = w.rect.y + w.rect.h / 2;
            const dx = wx - cx;
            const dy = wy - cy;
            let ahead = false;
            if (direction === Qt.Key_Left) {
                ahead = dx < 0;
            } else if (direction === Qt.Key_Right) {
                ahead = dx > 0;
            } else if (direction === Qt.Key_Up) {
                ahead = dy < 0;
            } else {
                ahead = dy > 0;
            }
            if (!ahead) {
                continue;
            }
            const distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = w;
            }
        }
        return best;
    }

    function moveSelection(key) {
        const cell = column.cellAt(root.selectedSlice);
        if (!cell) {
            return;
        }
        const ordered = root.orderedWindows(cell);
        if (root.selectedWindowId !== "") {
            const next = root.neighbourWindow(cell, key);
            if (next) {
                root.selectedWindowId = next.id;
                return;
            }
        } else if (ordered.length > 0 && (key === Qt.Key_Down || key === Qt.Key_Right)) {
            root.selectedWindowId = ordered[0].id;
            return;
        }
        // At the edge of the cell: Up/Down move to the neighbouring
        // workspace, Left/Right to the neighbouring screen's view.
        if (key === Qt.Key_Up || key === Qt.Key_Down) {
            const index = root.selectedSlice + (key === Qt.Key_Up ? -1 : 1);
            if (index >= 0 && index < root.slice.length) {
                root.selectedSliceOverride = index;
                root.selectedWindowId = "";
            }
            return;
        }
        const view = root.effect.getView(key === Qt.Key_Left ? Qt.LeftEdge : Qt.RightEdge);
        if (view) {
            root.selectedWindowId = "";
            root.effect.activateView(view);
        }
    }

    Keys.onPressed: event => {
        // Every key is consumed here: the compositor's effects filter has
        // already taken it and it cannot be forwarded anywhere.
        event.accepted = true;
        if (root.renaming) {
            return;
        }
        const cell = column.cellAt(root.selectedSlice);
        switch (event.key) {
        case Qt.Key_Escape:
            root.effect.deactivate();
            return;
        case Qt.Key_Left:
        case Qt.Key_Right:
        case Qt.Key_Up:
        case Qt.Key_Down:
            root.moveSelection(event.key);
            return;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (!cell) {
                return;
            }
            if (root.selectedWindowId !== "") {
                root.activateWindowIn(cell, root.selectedWindowId);
            } else {
                root.focusCellAndClose(cell);
            }
            return;
        case Qt.Key_F2:
            if (cell) {
                cell.nameLabel.startRename();
            }
            return;
        case Qt.Key_Delete:
            if (root.selectedWindowId !== "") {
                root.effect.closeWindow(root.selectedWindowId);
                root.selectedWindowId = "";
            }
            return;
        }
        if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
            const index = event.key - Qt.Key_1;
            if (index < root.slice.length) {
                root.effect.focusWorkspace(root.screenId, root.slice[index].id);
                root.effect.deactivate();
            }
        }
    }

    // A window that left the model is no longer selectable.
    onScreenModelChanged: {
        if (root.selectedWindowId === "") {
            return;
        }
        const cell = column.cellAt(root.selectedSlice);
        const still = cell && cell.windows.some(w => w.id === root.selectedWindowId);
        if (!still) {
            root.selectedWindowId = "";
        }
    }

    Component.onCompleted: {
        root.organized = true;
    }
}
