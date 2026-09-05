// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The drop target in the gap between two workspaces, above the first and
// below the last. gapIndex is the slice index a workspace created here
// would take: 0 = above the first workspace, N = below the last (niri's
// InsertWorkspace::NewAt). A window dropped here asks the daemon for a new
// workspace; a workspace label dropped here asks for a reorder to that
// index (or a transfer, when it came from another screen).

import QtQuick
import org.kde.kirigami as Kirigami

DropArea {
    id: gap

    required property Item root
    required property int gapIndex

    keys: ["pz-window", "pz-workspace"]

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Kirigami.Units.largeSpacing
        anchors.rightMargin: Kirigami.Units.largeSpacing
        radius: Kirigami.Units.cornerRadius
        color: Kirigami.Theme.highlightColor
        opacity: gap.containsDrag ? 0.35 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
            }
        }
    }

    onDropped: drop => {
        const payload = drop.source ? drop.source.payload : null;
        gap.root.dropIntoGap(payload, gap.gapIndex);
        drop.accept(Qt.MoveAction);
    }
}
