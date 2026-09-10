// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// One window inside a workspace cell: a live WindowThumbnail at the model's
// rect scaled by the current zoom. The thumbnail renders minimized and
// off-desktop windows alike and aspect-fits its content, which equals a
// crop here because the tile keeps the window's own aspect. clip keeps the
// window's shadow (which KWin draws outside frameGeometry) inside the tile.
//
// Input follows niri: a left press starts a drag grab and a release with no
// movement activates the window; a middle click closes it. The tile itself
// never moves. A drag hands the root's DragProxy a payload naming this
// window and its source cell, and the proxy travels with the pointer.

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kwin as KWinComponents

KWinComponents.WindowThumbnail {
    id: tile

    required property Item root
    required property Item cell
    // The model's window row: {id, rect: {x, y, w, h}, floating, minimized,
    // sticky, column, tile}.
    required property var win

    readonly property var handle: tile.root.effect.windowHandle(tile.win.id)
    readonly property real zoom: tile.root.zoom
    readonly property string windowId: tile.win.id
    // Sticky (all-desktops) windows are listed once on the current
    // workspace and are not draggable; the daemon refuses the verb anyway.
    readonly property bool draggable: !tile.win.sticky
    readonly property bool selected: tile.root.selectedWindowId === tile.windowId && tile.cell.sliceIndex === tile.root.selectedSlice
    // What a drop receiver needs to know about where this tile came from.
    readonly property var dragPayload: ({
            kind: "pz-window",
            windowId: tile.windowId,
            desktopId: tile.cell.desktopId,
            screenId: tile.root.screenId,
            windowCount: tile.cell.windows.length
        })

    wId: tile.handle
    visible: tile.handle !== undefined && tile.handle !== null && width > 0 && height > 0
    clip: true

    x: tile.root.snap(tile.win.rect.x * tile.zoom)
    y: tile.root.snap(tile.win.rect.y * tile.zoom)
    width: tile.root.snap(tile.win.rect.w * tile.zoom)
    height: tile.root.snap(tile.win.rect.h * tile.zoom)
    z: tile.root.effect.stackingIndex(tile.win.id)
    // A minimized window is drawn like any other, dimmed: the daemon lists
    // it at its last frame and the thumbnail still renders it.
    opacity: tile.win.minimized ? 0.5 : 1

    Accessible.role: Accessible.Button
    Accessible.name: i18nd("plasmazones", "Window")

    // Keyboard selection and pointer hover highlight.
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: Kirigami.Units.cornerRadius / 2
        border.width: Kirigami.Units.smallSpacing / 2
        border.color: Kirigami.Theme.highlightColor
        visible: tile.root.interactive && !tile.root.dragActive && (tile.selected || hover.hovered)
    }

    HoverHandler {
        id: hover
        enabled: tile.root.interactive
    }

    TapHandler {
        id: press
        acceptedButtons: Qt.LeftButton
        enabled: tile.root.interactive
        onPressedChanged: {
            if (pressed) {
                if (tile.draggable) {
                    tile.root.dragProxy.begin(tile, tile.dragPayload, point.pressPosition);
                }
            } else if (!drag.active) {
                tile.root.dragProxy.cancel();
            }
        }
        // A release with no movement: focus the workspace, activate the
        // window, close.
        onTapped: tile.root.activateWindowIn(tile.cell, tile.windowId)
    }

    TapHandler {
        acceptedButtons: Qt.MiddleButton
        enabled: tile.root.interactive
        onTapped: tile.root.effect.closeWindow(tile.windowId)
    }

    DragHandler {
        id: drag
        target: null
        acceptedButtons: Qt.LeftButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        enabled: tile.root.interactive && tile.draggable
        onActiveChanged: {
            if (active) {
                tile.root.dragProxy.show();
            } else {
                tile.root.dragProxy.finish(centroid.scenePosition);
            }
        }
        onActiveTranslationChanged: if (active) {
            tile.root.dragProxy.follow(activeTranslation);
        }
    }
}
