// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Plasma shell surfaces: the applet popups (application launcher, system tray
// flyouts, a widget's expanded view). Appearance rows like the Windows page,
// but on the `shell` root, which resolves its shader inside itself
// (shaderPathIsolationRoot) — nothing set for the user's own windows reaches
// these surfaces, so every row here reads "no shader" until the user picks one.
//
// "All Shell Surfaces" is the `shell` cascade parent: its shader and timing
// apply to both legs by inheritance, and either leg can override it.
//
// Panels have no rows: a panel is mapped once for the session and hides by
// sliding under the screen edge rather than by closing, so it reaches no
// window-lifecycle hook there is a leg for (see animationEventPathFor).
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Plasma shell animation events")
    headerText: i18n("Animations for the surfaces the Plasma shell owns, like the application launcher and the system tray popups. No shader you pick for your own windows reaches them, so every row starts with no shader and picking one is the whole opt-in. Until you pick one, these surfaces animate the way Plasma animates them. \"All Shell Surfaces\" is the default for both events, and either can override it. Panels have no rows here, because a panel stays on screen for the whole session instead of opening and closing.")
    eventModel: [
        {
            "eventPath": "shell",
            "eventLabel": i18n("All Shell Surfaces"),
            "isParentNode": true
        },
        {
            "eventPath": "shell.appletPopup.show",
            "eventLabel": i18n("Applet Popup Shown"),
            "isParentNode": false
        },
        {
            "eventPath": "shell.appletPopup.hide",
            "eventLabel": i18n("Applet Popup Hidden"),
            "isParentNode": false
        }
    ]
}
