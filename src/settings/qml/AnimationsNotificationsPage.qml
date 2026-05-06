// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Notifications animation page — the `osd.*` resolver subtree (genuine
// OSD surface, both LayoutOsd and NavigationOsd modes). The "All
// Notifications" parent is a parent-node card whose override cascades
// to `osd.show` and `osd.hide` via ShaderProfileTree::resolve's
// walk-up. Popup-family events (zone selector, layout picker, snap
// assist) live under `popup.*` and have their own dedicated page.
Flickable {
    contentHeight: col.implicitHeight
    clip: true
    Accessible.name: i18n("Notification animation events")

    ColumnLayout {
        id: col

        width: parent.width
        spacing: Kirigami.Units.smallSpacing

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "osd"
            eventLabel: i18n("All Notifications")
            isParentNode: true
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "osd.show"
            eventLabel: i18n("Show")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "osd.hide"
            eventLabel: i18n("Hide")
        }

    }

}
