// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// OSDs animation page: the `osd.*` resolver subtree (both LayoutOsd
// and NavigationOsd modes). The "All OSDs" parent is a parent-node
// card whose override cascades to `osd.show` and `osd.hide` via
// ShaderProfileTree::resolve's walk-up. Transient overlays (zone
// selector, layout picker, snap assist) live under `popup.*` and have
// their own dedicated Overlays page.
SettingsFlickable {
    contentHeight: col.implicitHeight
    clip: true
    Accessible.name: i18n("OSD animation events")

    ColumnLayout {
        id: col

        width: parent.width
        spacing: Kirigami.Units.smallSpacing

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "osd"
            eventLabel: i18n("All OSDs")
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
