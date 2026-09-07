// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Decoration → Shell. Viewport-virtualized by DecorationSurfaceCardList; thin
// model declaration like the other decoration sub-pages, with i18n() labels in
// QML.
//
// Unlike the window surfaces, these belong to a shell rather than to a
// managed window. The `shell.panel` and `shell.appletPopup` cards are
// plasmashell's own surfaces; the `shell.phosphor.*` cards are the Phosphor
// shell's, which PlasmaZones does own. The `shell` subtree is baseline-isolated
// (it never inherits the global default chain), none of these surfaces honours
// the plain border / opacity settings or the window rules, and there is no
// separate enable toggle — a pack chain engaged here is the whole decoration
// and the whole opt-in. "All Shell Surfaces" (path "shell") is the category
// root card that each surface can override via the DecorationProfileTree
// walk-up.
DecorationSurfaceCardList {
    Accessible.name: i18n("Shell decoration surfaces")
    headerText: i18n("Decoration for surfaces a shell owns: the Plasma panels and applet popups, and the Phosphor shell chrome. The global default decoration never applies to them. Plasma surfaces stay undecorated until a decoration is enabled here, while the Phosphor surfaces start with the decoration the Phosphor shell ships.")
    surfaceModel: [
        {
            "surfacePath": "shell",
            "cardLabel": i18nc("@item the category root card that every shell surface inherits from", "All Shell Surfaces"),
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
            "cardLabel": i18nc("@item the category root card that every Phosphor shell surface inherits from", "All Phosphor Shell Surfaces"),
            "alwaysEnabled": false,
            "isParentNode": true
        },
        {
            "surfacePath": "shell.phosphor.bar",
            "cardLabel": i18nc("@item the Phosphor shell's top bar surface, not a progress or menu bar", "Bar"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "shell.phosphor.popout",
            "cardLabel": i18nc("@item panels that pop out from the Phosphor shell bar", "Popouts"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "shell.phosphor.osd",
            "cardLabel": i18nc("@item the Phosphor shell's on-screen display bands", "OSD Bands"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "shell.phosphor.notification",
            "cardLabel": i18nc("@item the Phosphor shell's notification toasts", "Notifications"),
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
