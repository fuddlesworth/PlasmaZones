// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// One workspace of the column: its live windows at their real positions,
// scaled by the current zoom, clipped to the cell (a scrolling strip's
// parked columns lie outside it), with a shadow, a current-workspace
// highlight and a label. The window rects come from the daemon's model, so
// a non-current scrolling workspace renders where its engine says its
// columns are rather than where KWin last committed them.
//
// Input: the cell is a DropArea for windows and workspace labels, a left
// click on empty area switches this screen to the workspace and closes, and
// a right drag on a scrolling workspace pans its strip.

import QtQuick
import org.kde.kirigami as Kirigami

DropArea {
    id: cell

    required property Item root
    required property Item column
    required property int sliceIndex
    // The map's slice entry: {id, index, name?, current?}.
    required property var entry
    property bool placeholder: false

    readonly property string desktopId: cell.entry && cell.entry.id ? cell.entry.id : ""
    readonly property bool current: !cell.placeholder && cell.sliceIndex === cell.root.currentIndex
    readonly property bool highlighted: !cell.placeholder && cell.sliceIndex === cell.root.selectedSlice
    readonly property bool pinned: !cell.placeholder && !!(cell.entry && cell.entry.name)
    // The model's workspace object for this desktop, or null while the
    // model has not arrived (the cell then shows only its frame).
    readonly property var workspace: {
        const model = cell.root.screenModel;
        if (!model || !model.workspaces || !cell.desktopId) {
            return null;
        }
        const ws = model.workspaces[cell.desktopId];
        return ws ? ws : null;
    }
    readonly property string mode: cell.workspace ? cell.workspace.mode : "none"
    readonly property var windows: (cell.workspace && Array.isArray(cell.workspace.windows)) ? cell.workspace.windows : []
    readonly property bool droppable: !cell.placeholder && cell.desktopId !== "" && cell.mode !== "none"
    // A strip's main axis is horizontal unless every column sits at the
    // same x, in which case the columns are stacked vertically.
    readonly property bool stripVertical: {
        const strip = cell.workspace ? cell.workspace.strip : null;
        if (!strip || !Array.isArray(strip.columns) || strip.columns.length < 2) {
            return false;
        }
        const x0 = strip.columns[0].rect ? strip.columns[0].rect.x : 0;
        for (let i = 1; i < strip.columns.length; ++i) {
            if (!strip.columns[i].rect || strip.columns[i].rect.x !== x0) {
                return false;
            }
        }
        return true;
    }
    readonly property string label: {
        if (cell.placeholder) {
            return i18nd("plasmazones", "No workspaces yet");
        }
        if (cell.entry && cell.entry.name) {
            return cell.entry.name;
        }
        // A dynamic workspace carries no declaration; its label is whatever
        // KWin calls the desktop (a rename pushes there). The revision read
        // is what re-evaluates this when a name moves.
        void cell.root.effect.desktopNamesRevision;
        const kwinName = cell.desktopId ? cell.root.effect.desktopName(cell.desktopId) : "";
        if (kwinName) {
            return kwinName;
        }
        return i18nd("plasmazones", "Workspace %1", cell.entry ? cell.entry.index : 0);
    }
    readonly property alias nameLabel: nameLabel

    keys: ["pz-window", "pz-workspace"]

    // Not Item.enabled: that would also switch off the tiles' handlers. A
    // placeholder or mode-less cell refuses the drag at entry instead, so it
    // never shows a drop hover and the proxy springs back.
    onEntered: drag => {
        drag.accepted = cell.droppable;
    }
    onDropped: drop => {
        const payload = drop.source ? drop.source.payload : null;
        cell.root.dropOntoCell(payload, cell, Qt.point(drop.x, drop.y));
        drop.accept(Qt.MoveAction);
    }

    // Workspace shadow (niri's workspace-shadow, always on), normalized to a
    // 1080px-tall screen and zoomed with the cell so it never overlaps the
    // gap on small outputs.
    Kirigami.ShadowedRectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
        opacity: cell.placeholder ? 0.25 : 0.4
        radius: cell.root.snap(Kirigami.Units.cornerRadius * cell.root.progress)
        shadow {
            size: cell.root.snap(40 * (cell.root.height / 1080) * cell.root.zoom) * cell.root.progress
            yOffset: cell.root.snap(10 * (cell.root.height / 1080) * cell.root.zoom) * cell.root.progress
            color: Qt.rgba(0, 0, 0, 0.5 * cell.root.progress)
        }
        border {
            width: cell.current ? cell.root.snap(Kirigami.Units.smallSpacing) * cell.root.progress : 0
            color: Kirigami.Theme.highlightColor
        }
    }

    // Keyboard highlight (the workspace the arrow keys and F2 act on) and
    // the drop hover, drawn as a soft wash inside the frame.
    Rectangle {
        anchors.fill: parent
        radius: cell.root.snap(Kirigami.Units.cornerRadius * cell.root.progress)
        color: Kirigami.Theme.highlightColor
        opacity: cell.containsDrag ? 0.25 : (cell.highlighted && cell.root.selectedWindowId === "" && cell.root.interactive ? 0.12 : 0)
        Behavior on opacity {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
            }
        }
    }

    HoverHandler {
        id: hover
        enabled: cell.root.interactive
    }

    // Left click on empty cell area: switch this screen to the workspace and
    // close, focus unchanged. Tiles sit above this handler and take their
    // own presses first.
    TapHandler {
        acceptedButtons: Qt.LeftButton
        enabled: cell.root.interactive && !cell.placeholder
        onTapped: {
            const p = cell.mapToItem(surface, point.pressPosition.x, point.pressPosition.y);
            if (surface.childAt(p.x, p.y)) {
                return;
            }
            cell.root.focusCellAndClose(cell);
        }
    }

    // Right drag on a scrolling workspace pans its strip. Steps are
    // coalesced by a short timer and sent unzoomed; the sign follows the
    // content (drag right moves the strip right, so the view moves left).
    DragHandler {
        id: pan
        target: null
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        enabled: cell.root.interactive && cell.mode === "scrolling"
        // Unzoomed pixels already sent this drag; the next step is the
        // rounded difference to the total the pointer asks for, so rounding
        // never loses distance across steps.
        property int sentTotal: 0
        onActiveChanged: {
            if (active) {
                pan.sentTotal = 0;
            } else {
                panFlush.stop();
                cell.flushPan();
            }
        }
        onActiveTranslationChanged: if (active && !panFlush.running) {
            panFlush.start();
        }
    }
    Timer {
        id: panFlush
        interval: 33
        onTriggered: cell.flushPan()
    }
    function flushPan() {
        const along = cell.stripVertical ? pan.activeTranslation.y : pan.activeTranslation.x;
        const wanted = -along / cell.root.zoom;
        const step = Math.round(wanted - pan.sentTotal);
        if (step === 0) {
            return;
        }
        pan.sentTotal += step;
        cell.root.effect.panStrip(cell.root.screenId, cell.desktopId, step);
    }

    Item {
        id: surface
        anchors.fill: parent
        clip: true

        Repeater {
            model: cell.windows
            delegate: WindowTile {
                required property var modelData
                root: cell.root
                cell: cell
                win: modelData
            }
        }
    }

    WorkspaceLabel {
        id: nameLabel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.top
        anchors.bottomMargin: Kirigami.Units.smallSpacing
        root: cell.root
        cell: cell
        text: cell.label
        current: cell.current
        pinned: cell.pinned
        inert: cell.placeholder
        cellHovered: hover.hovered
        visible: cell.root.effect.showWorkspaceNames || cell.placeholder
        opacity: cell.root.progress
    }
}
