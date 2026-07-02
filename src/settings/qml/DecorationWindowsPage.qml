// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Decoration → Windows. Viewport-virtualized by DecorationSurfaceCardList;
// thin model declaration like AnimationsWindowsPage. Labels are i18n() here in
// QML (not derived in C++), matching the animation page models.
//
// "All Windows" (path "window") is the alwaysEnabled root that carries the
// window decoration default; an override on a child placement-state card
// (window.tiled / window.snapped / window.floating) diverges from it via the
// DecorationProfileTree walk-up. Title bars are a window concept, so every
// window card exposes the hide-title-bar toggle.
DecorationSurfaceCardList {
    Accessible.name: i18n("Window decoration surfaces")
    headerText: i18n("Decoration for windows. \"All Windows\" is the default; each placement state can override it.")
    surfaceModel: [
        {
            "surfacePath": "window",
            "cardLabel": i18n("All Windows"),
            "alwaysEnabled": true,
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
