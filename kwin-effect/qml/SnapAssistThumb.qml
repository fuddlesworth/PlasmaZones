// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kwin as KWinComponents

// Offscreen scene loaded by KWin::OffscreenQuickScene to grab a thumbnail of
// a single window. The C++ side drives @c wId and @c boxSize via setProperty
// on the root before issuing OffscreenQuickView::update() — WindowThumbnail
// renders through the live compositor texture, so no second pass over the
// window is needed.
Item {
    id: root

    // QUuid of the EffectWindow being captured (EffectWindow::internalId()).
    property var wId
    // Bounding box for the thumbnail; the WindowThumbnail honours its source
    // window's aspect ratio inside this rect.
    property size boxSize: Qt.size(256, 256)

    width: boxSize.width
    height: boxSize.height

    KWinComponents.WindowThumbnail {
        anchors.fill: parent
        wId: root.wId
    }

}
