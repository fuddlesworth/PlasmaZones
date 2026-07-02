// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Decoration → OSDs. Viewport-virtualized by DecorationSurfaceCardList; thin
// model declaration like the animation sub-pages. The OSD surface is its own
// alwaysEnabled root (no global decoration default to inherit). No title bar.
DecorationSurfaceCardList {
    Accessible.name: i18n("OSD decoration surface")
    headerText: i18n("Decoration for the on-screen display.")
    surfaceModel: [
        {
            "surfacePath": "osd",
            "cardLabel": i18n("On-Screen Display"),
            "alwaysEnabled": true,
            "isParentNode": false
        }
    ]
}
