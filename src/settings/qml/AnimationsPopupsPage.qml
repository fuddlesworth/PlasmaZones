// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Popups animation page — the `panel.*` resolver subtree. The "All
// Popups" parent is a parent-node card whose override cascades to
// `panel.popup.zoneSelector.*`, `panel.popup.layoutPicker.*` and
// `panel.popup.snapAssist.show` via ShaderProfileTree::resolve's
// walk-up (`panel.popup` is the closest common ancestor of all three
// popup surfaces). `panel.slideIn` is a sibling under `panel` that
// drives the settings-panel slide-in animation; it shares the page
// because it's panel-family but doesn't inherit from the popup node.
Flickable {
    contentHeight: col.implicitHeight
    clip: true
    Accessible.name: i18n("Popup animation events")

    ColumnLayout {
        id: col

        width: parent.width
        spacing: Kirigami.Units.smallSpacing

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup"
            eventLabel: i18n("All Popups")
            isParentNode: true
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup.zoneSelector.show"
            eventLabel: i18n("Zone Selector — Show")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup.zoneSelector.hide"
            eventLabel: i18n("Zone Selector — Hide")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup.layoutPicker.show"
            eventLabel: i18n("Layout Picker — Show")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup.layoutPicker.hide"
            eventLabel: i18n("Layout Picker — Hide")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup.snapAssist.show"
            eventLabel: i18n("Snap Assist — Show")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.slideIn"
            eventLabel: i18n("Slide In")
        }

    }

}
