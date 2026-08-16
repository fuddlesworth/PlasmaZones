// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Decoration → Shell. Viewport-virtualized by DecorationSurfaceCardList; thin
// model declaration like the other decoration sub-pages, with i18n() labels in
// QML.
//
// Unlike every other decoration surface, these are windows PlasmaZones does not
// own: they belong to plasmashell. The `shell` subtree is baseline-isolated
// (it never inherits the global default chain), none of these surfaces honours
// the plain border / opacity settings or the window rules, and there is no
// separate enable toggle — a pack chain engaged here is the whole decoration
// and the whole opt-in. "All Shell Surfaces" (path "shell") is the category
// root card that each surface can override via the DecorationProfileTree
// walk-up.
DecorationSurfaceCardList {
    Accessible.name: i18n("Shell decoration surfaces")
    headerText: i18n("Decoration for surfaces owned by the Plasma shell. These stay undecorated until a decoration is enabled here, and the global default decoration never applies to them.")
    surfaceModel: [
        {
            "surfacePath": "shell",
            "cardLabel": i18n("All Shell Surfaces"),
            "alwaysEnabled": false,
            "isParentNode": true
        },
        {
            "surfacePath": "shell.panel",
            "cardLabel": i18n("Panels"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "shell.appletPopup",
            "cardLabel": i18n("Applet Popups"),
            "alwaysEnabled": false,
            "isParentNode": false
        }
    ]
}
