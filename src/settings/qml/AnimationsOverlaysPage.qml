// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Overlays animation page — the `popup.*` resolver subtree (zone
// selector, layout picker, snap assist; the resolver path keeps the
// historical name). The "All Overlays" parent is a parent-node card
// whose override cascades to `popup.zoneSelector.*`,
// `popup.layoutPicker.*` and `popup.snapAssist.*` via
// ShaderProfileTree::resolve's walk-up (`popup` is the closest common
// ancestor of all three surfaces). In-app side panels (settings nav
// rail, editor property panel) live under `panel.*` and have their
// own dedicated Side Panels page.
SettingsFlickable {
    contentHeight: col.implicitHeight
    clip: true
    Accessible.name: i18n("Overlay animation events")

    ColumnLayout {
        id: col

        width: parent.width
        spacing: Kirigami.Units.smallSpacing

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "popup"
            eventLabel: i18n("All Overlays")
            isParentNode: true
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "popup.zoneSelector.show"
            eventLabel: i18n("Zone Selector — Show")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "popup.zoneSelector.hide"
            eventLabel: i18n("Zone Selector — Hide")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "popup.layoutPicker.show"
            eventLabel: i18n("Layout Picker — Show")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "popup.layoutPicker.hide"
            eventLabel: i18n("Layout Picker — Hide")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "popup.snapAssist.show"
            eventLabel: i18n("Snap Assist — Show")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "popup.snapAssist.hide"
            eventLabel: i18n("Snap Assist — Hide")
        }

    }

}
