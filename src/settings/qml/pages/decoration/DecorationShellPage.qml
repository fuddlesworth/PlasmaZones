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
    headerText: i18n("Decoration for surfaces a shell owns: the Plasma panels and applet popups, and the Phosphor shell chrome. The global default decoration never applies to them. Plasma surfaces stay undecorated until a decoration is enabled here, while the Phosphor surfaces start on the spectrum defaults.")
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
        },
        {
            "surfacePath": "shell.phosphor",
            "cardLabel": i18n("All Phosphor Shell Surfaces"),
            "alwaysEnabled": false,
            "isParentNode": true
        },
        {
            "surfacePath": "shell.phosphor.bar",
            "cardLabel": i18n("Bar"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "shell.phosphor.popout",
            "cardLabel": i18n("Popouts"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "shell.phosphor.osd",
            "cardLabel": i18n("OSD Bands"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "shell.phosphor.notification",
            "cardLabel": i18n("Notifications"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "shell.phosphor.picker",
            "cardLabel": i18n("Wallpaper Picker"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "shell.phosphor.lock",
            "cardLabel": i18n("Lock Screen"),
            "alwaysEnabled": false,
            "isParentNode": false
        }
    ]
}
