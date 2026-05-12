// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Layout-editor zone-manipulation animations. Fires only inside the
// PlasmaZones layout editor (fill-preview, snap-resize-preview).
// Runtime window snapping is KWin's compositor-level domain and is
// NOT controlled here.
SettingsFlickable {
    contentHeight: col.implicitHeight
    clip: true
    Accessible.name: i18n("Layout editor animation events")

    ColumnLayout {
        id: col

        width: parent.width
        spacing: Kirigami.Units.smallSpacing

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "editor"
            eventLabel: i18n("All Editor Events")
            isParentNode: true
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "editor.snapIn"
            eventLabel: i18n("Snap In (Fill)")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "editor.snapOut"
            eventLabel: i18n("Snap Out")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "editor.snapResize"
            eventLabel: i18n("Snap Resize (Drag Preview)")
        }

    }

}
