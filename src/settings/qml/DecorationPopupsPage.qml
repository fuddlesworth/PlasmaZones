// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Decoration → Popups. Viewport-virtualized by DecorationSurfaceCardList; thin
// model declaration like the animation sub-pages, with i18n() labels in QML.
//
// "All Popups" (path "popup") is the category root card for the transient
// popups (snap assist, zone selector, layout picker); each can override it via
// the DecorationProfileTree walk-up. Popups have no title bar, so none expose
// the toggle.
DecorationSurfaceCardList {
    Accessible.name: i18n("Popup decoration surfaces")
    headerText: i18n("Decoration for the transient popups. \"All Popups\" is the default. Each popup can override it.")
    surfaceModel: [
        {
            "surfacePath": "popup",
            "cardLabel": i18n("All Popups"),
            "alwaysEnabled": false,
            "isParentNode": true
        },
        {
            "surfacePath": "popup.snapAssist",
            "cardLabel": i18n("Snap Assist"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "popup.zoneSelector",
            "cardLabel": i18n("Zone Selector"),
            "alwaysEnabled": false,
            "isParentNode": false
        },
        {
            "surfacePath": "popup.layoutPicker",
            "cardLabel": i18n("Layout Picker"),
            "alwaysEnabled": false,
            "isParentNode": false
        }
    ]
}
