// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// One workspace of the column: its live windows at their real positions,
// scaled by the current zoom, clipped to the cell (a scrolling strip's
// parked columns lie outside it), with a shadow, a current-workspace
// highlight and a label. The window rects come from the daemon's model, so
// a non-current scrolling workspace renders where its engine says its
// columns are rather than where KWin last committed them.

import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: cell

    required property Item root
    required property Item column
    required property int sliceIndex
    // The map's slice entry: {id, index, name?, current?}.
    required property var entry
    property bool placeholder: false

    readonly property string desktopId: cell.entry && cell.entry.id ? cell.entry.id : ""
    readonly property bool current: !cell.placeholder && cell.sliceIndex === cell.root.currentIndex
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
    readonly property string label: {
        if (cell.placeholder) {
            return i18nd("plasmazones", "No workspaces yet");
        }
        if (cell.entry && cell.entry.name) {
            return cell.entry.name;
        }
        return i18nd("plasmazones", "Workspace %1", cell.entry ? cell.entry.index : 0);
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

    Item {
        id: surface
        anchors.fill: parent
        clip: true

        Repeater {
            model: cell.windows
            delegate: WindowTile {
                required property var modelData
                root: cell.root
                win: modelData
            }
        }
    }

    WorkspaceLabel {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.top
        anchors.bottomMargin: Kirigami.Units.smallSpacing
        text: cell.label
        current: cell.current
        visible: cell.root.effect.showWorkspaceNames || cell.placeholder
        opacity: cell.root.progress
    }
}
