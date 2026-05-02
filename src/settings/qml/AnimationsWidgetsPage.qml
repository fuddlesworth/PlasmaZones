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
            eventPath: "widget"
            eventLabel: i18n("All Widget Events")
            isParentNode: true
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.hover"
            eventLabel: i18n("Hover")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.press"
            eventLabel: i18n("Press")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.toggle"
            eventLabel: i18n("Toggle")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.badge"
            eventLabel: i18n("Badge")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.tint"
            eventLabel: i18n("Tint")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.dim"
            eventLabel: i18n("Dim")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.fade"
            eventLabel: i18n("Fade")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.reorder"
            eventLabel: i18n("Reorder")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.accordion"
            eventLabel: i18n("Accordion")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.progress"
            eventLabel: i18n("Progress")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "cursor.hover"
            eventLabel: i18n("Cursor Hover")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "cursor.click"
            eventLabel: i18n("Cursor Click")
        }
    }
}
