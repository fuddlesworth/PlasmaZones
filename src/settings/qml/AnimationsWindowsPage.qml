// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Window animation events. The card list is viewport-virtualized by
// AnimationEventCardList (only visible AnimationEventCards build) — see
// that component for the rationale.
//
// `window.snapResize` (resize-only branch of applyWindowGeometry) is
// intentionally NOT listed: no kwin-effect callsite routes a resize-only
// event through tryBeginShaderForEvent today, so a card here would be
// runtime-dead.
AnimationEventCardList {
    Accessible.name: i18n("Window animation events")
    eventModel: [
        {
            "eventPath": "window",
            "eventLabel": i18n("All Window Events"),
            "isParentNode": true
        },
        {
            "eventPath": "window.open",
            "eventLabel": i18n("Open"),
            "isParentNode": false
        },
        {
            "eventPath": "window.close",
            "eventLabel": i18n("Close"),
            "isParentNode": false
        },
        {
            "eventPath": "window.minimize",
            "eventLabel": i18n("Minimize"),
            "isParentNode": false
        },
        {
            "eventPath": "window.maximize",
            "eventLabel": i18n("Maximize"),
            "isParentNode": false
        },
        {
            "eventPath": "window.move",
            "eventLabel": i18n("Move"),
            "isParentNode": false
        },
        {
            "eventPath": "window.resize",
            "eventLabel": i18n("Resize"),
            "isParentNode": false
        },
        {
            "eventPath": "window.focus",
            "eventLabel": i18n("Focus"),
            "isParentNode": false
        },
        // Snap-into-zone window animations driven by the kwin-effect. The
        // window quad animates when it snaps into / out of a zone or when a
        // layout switch repositions it.
        {
            "eventPath": "window.snapIn",
            "eventLabel": i18n("Snap Into Zone"),
            "isParentNode": false
        },
        {
            "eventPath": "window.snapOut",
            "eventLabel": i18n("Snap Out of Zone"),
            "isParentNode": false
        },
        {
            "eventPath": "window.layoutSwitch",
            "eventLabel": i18n("Layout Switch"),
            "isParentNode": false
        }
    ]
}
