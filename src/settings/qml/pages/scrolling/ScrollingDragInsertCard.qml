// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The scrolling drag re-insert Triggers card, the peer of the
 * Triggers card on the Tiling → Window page. The trigger list and its
 * derived always-re-insert switch go through the scrollingBehaviorPage
 * sub-controller (the AlwaysActive sentinel lives inside the stored list);
 * the toggle-mode switch binds appSettings directly like its tiling twin.
 */
SettingsCard {
    id: root

    readonly property var settingsBridge: settingsController.scrollingBehaviorPage

    headerText: i18n("Triggers")
    searchAnchor: "scrollingTriggers"
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Always re-insert on drag")
            searchAnchor: "scrollingAlwaysReinsertOnDrag"
            description: i18n("Dragged windows re-insert into the scroll strip at the cursor position without requiring a modifier key or mouse button")

            SettingsSwitch {
                id: alwaysReinsertSwitch

                checked: root.settingsBridge.alwaysReinsertIntoStrip
                accessibleName: i18n("Always re-insert into strip on drag")
                onToggled: function (newValue) {
                    root.settingsBridge.alwaysReinsertIntoStrip = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Hold to re-insert into strip")
            searchAnchor: "scrollingHoldToReinsert"
            description: i18n("Hold a modifier or mouse button while dragging a window to insert it into the scroll strip at the cursor position, as a new column or stacked into an existing one")
            enabled: !alwaysReinsertSwitch.checked

            ModifierAndMouseCheckBoxes {
                width: Kirigami.Units.gridUnit * 16
                allowMultiple: true
                acceptMode: acceptModeAll
                triggers: root.settingsBridge.scrollingDragInsertTriggers
                defaultTriggers: root.settingsBridge.defaultScrollingDragInsertTriggers
                tooltipEnabled: false
                onTriggersModified: triggers => {
                    root.settingsBridge.scrollingDragInsertTriggers = triggers;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Toggle mode")
            searchAnchor: "scrollingTriggersToggleMode"
            description: i18n("Tap the re-insert trigger once to activate the strip preview, tap again to deactivate it")
            enabled: !alwaysReinsertSwitch.checked

            SettingsSwitch {
                checked: appSettings.scrollingDragInsertToggle
                accessibleName: i18n("Toggle mode for re-insert into strip")
                onToggled: function (newValue) {
                    appSettings.scrollingDragInsertToggle = newValue;
                }
            }
        }
    }
}
