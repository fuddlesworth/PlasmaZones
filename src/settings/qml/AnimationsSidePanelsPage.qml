// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Side panels animation page — the `panel.*` resolver subtree (the
// resolver path keeps the historical name). Covers in-app side
// surfaces that slide in from an edge (settings nav rail, editor
// property panel). Slide is size/translate motion; fade is opacity.
// Renamed from "Panels" to disambiguate from system panels (Plasma
// taskbar, etc.) which we do not animate. Transient overlays (zone
// selector, layout picker, snap assist) live under `popup.*` and
// have their own dedicated Overlays page.
SettingsFlickable {
    contentHeight: col.implicitHeight
    clip: true
    Accessible.name: i18n("Side panel animation events")

    ColumnLayout {
        id: col

        width: parent.width
        spacing: Kirigami.Units.smallSpacing

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel"
            eventLabel: i18n("All Side Panels")
            isParentNode: true
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.slideIn"
            eventLabel: i18n("Slide In")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.slideOut"
            eventLabel: i18n("Slide Out")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.fadeIn"
            eventLabel: i18n("Fade In")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.fadeOut"
            eventLabel: i18n("Fade Out")
        }

    }

}
