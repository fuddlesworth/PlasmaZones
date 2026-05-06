// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Popups animation page — the `popup.*` resolver subtree. The "All
// Popups" parent is a parent-node card whose override cascades to
// `popup.zoneSelector.*`, `popup.layoutPicker.*` and
// `popup.snapAssist.show` via ShaderProfileTree::resolve's walk-up
// (`popup` is the closest common ancestor of all three popup surfaces).
// SnapAssist's hide leg is intentionally omitted — the surface
// destroys on hide before any frame paints, so a profile assignment
// there would be runtime-dead (see `animationshadersupportedpaths.h`
// and `OverlayService::buildSnapAssistConfig`).
// In-app side panels (settings nav rail, editor property panel) live
// under `panel.*` and have their own dedicated Panels page.
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
            eventPath: "popup"
            eventLabel: i18n("All Popups")
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

    }

}
