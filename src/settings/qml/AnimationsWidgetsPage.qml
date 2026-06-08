// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    contentHeight: col.implicitHeight
    clip: true
    Accessible.name: i18n("Widget animation events")

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
            eventPath: "widget.toggleOn"
            eventLabel: i18n("Toggle On")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.toggleOff"
            eventLabel: i18n("Toggle Off")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.badgeShow"
            eventLabel: i18n("Show (badge)")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.badgeHide"
            eventLabel: i18n("Hide (badge)")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.badgePulse"
            eventLabel: i18n("Pulse (badge)")
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
            eventPath: "widget.fadeIn"
            eventLabel: i18n("Fade In")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.fadeOut"
            eventLabel: i18n("Fade Out")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.reorder"
            eventLabel: i18n("Reorder")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.accordionExpand"
            eventLabel: i18n("Expand (accordion)")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.accordionCollapse"
            eventLabel: i18n("Collapse (accordion)")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.progress"
            eventLabel: i18n("Progress")
        }

        // Zone-rect widget. Embedded across the runtime overlay,
        // settings dialogs, layout thumbnails, the new-layout dialog,
        // shared previews. The animation lives with the widget; the
        // surface hosting it is incidental.
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.zoneHighlight"
            eventLabel: i18n("Zone Highlight")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.zoneHighlight.pop"
            eventLabel: i18n("Zone Highlight: Pop")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.zoneHighlight.border"
            eventLabel: i18n("Zone Highlight: Border")
        }

        // One-shot flash on the main zone-overlay surface when the
        // active layout switches mid-drag.
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "widget.zoneOverlayFlash"
            eventLabel: i18n("Zone Overlay: Layout-Switch Flash")
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
