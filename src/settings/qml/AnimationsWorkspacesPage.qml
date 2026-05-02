// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    contentHeight: col.implicitHeight
    clip: true
    ColumnLayout {
        id: col
        width: parent.width
        spacing: Kirigami.Units.smallSpacing
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "workspace"
            eventLabel: i18n("All Workspace Events")
            isParentNode: true
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "workspace.switchIn"
            eventLabel: i18n("Switch In")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "workspace.switchOut"
            eventLabel: i18n("Switch Out")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "workspace.overview"
            eventLabel: i18n("Overview")
        }
    }
}
