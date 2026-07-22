// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Decoration → Windows. Viewport-virtualized by DecorationSurfaceCardList;
// thin model declaration like AnimationsWindowsPage. Labels are i18n() here in
// QML (not derived in C++), matching the animation page models.
//
// "All Windows" (path "window") is the category root card. Its toggle
// engages the "window" override for editing; OFF clears it so the whole
// category inherits the (empty) baseline and windows render undecorated,
// window decoration default; an override on a child placement-state card
// (window.tiled / window.snapped / window.floating) diverges from it via the
// DecorationProfileTree walk-up.
DecorationSurfaceCardList {
    Accessible.name: i18n("Window decoration surfaces")
    headerText: i18n("Decoration for windows. \"All Windows\" is the default. Each placement state can override it.")
    surfaceModel: [
        {
            "surfacePath": "window",
            "cardLabel": i18n("All Windows"),
            "alwaysEnabled": false,
            "isParentNode": true
        },
        {
            "surfacePath": "window.tiled",
            "cardLabel": i18n("Tiled"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "window.snapped",
            "cardLabel": i18n("Snapped"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "window.floating",
            "cardLabel": i18n("Floating"),
            "alwaysEnabled": false,
            "isParentNode": false
        }
    ]
}
