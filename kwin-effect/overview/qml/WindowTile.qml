// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// One window inside a workspace cell: a live WindowThumbnail at the model's
// rect scaled by the current zoom. The thumbnail renders minimized and
// off-desktop windows alike and aspect-fits its content, which equals a
// crop here because the tile keeps the window's own aspect. clip keeps the
// window's shadow (which KWin draws outside frameGeometry) inside the tile.

import QtQuick
import org.kde.kwin as KWinComponents

KWinComponents.WindowThumbnail {
    id: tile

    required property Item root
    // The model's window row: {id, rect: {x, y, w, h}, floating, minimized,
    // sticky, column, tile}.
    required property var win

    readonly property var handle: tile.root.effect.windowHandle(tile.win.id)
    readonly property real zoom: tile.root.zoom

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
}
