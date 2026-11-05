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
    // Width for the SettingsRow control slot comes from TriggerLabels, which
    // every trigger editor host shares. Below ~640 px of row width the slot
    // clamp cuts the widget's right edge; shared pre-existing behaviour with
    // the tiling twin.

    headerText: i18n("Triggers")
    searchAnchor: "scrollingTriggers"
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Always re-insert on drag")
            searchAnchor: "scrollingAlwaysReinsertOnDrag"
            description: i18n("Dragged windows re-insert into the scroll strip at the cursor position without requiring a modifier key or mouse button. The trigger below then floats the window instead.")

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

        // Dual purpose, like the snapping activation triggers (#249): with
        // "Always re-insert on drag" on, the same triggers keep a drag OUT of
        // the strip so the window drops as a floating one (discussion #1162).
        // The Toggle mode row below inverts with it.
        SettingsRow {
            title: alwaysReinsertSwitch.checked ? i18n("Hold to float instead") : i18n("Hold to re-insert into strip")
            searchAnchor: "scrollingHoldToReinsert"
            description: alwaysReinsertSwitch.checked ? i18n("Hold a modifier or mouse button while dragging a window to float it instead of re-inserting it into the strip.") : i18n("Hold a modifier or mouse button while dragging a window to insert it into the strip under the cursor. It becomes a new column, or stacks into the column it lands on.")

            ModifierAndMouseCheckBoxes {
                width: TriggerLabels.editorPreferredWidth
                acceptMode: acceptModeAll
                // The always-active sentinel occupies one of the four stored
                // slots and is stripped out of the list below, so Add has to
                // stop one early or the merge silently drops the newest chip.
                reservedTriggerSlots: alwaysReinsertSwitch.checked ? 1 : 0
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
            description: alwaysReinsertSwitch.checked ? i18n("Tap the trigger once to float the dragged window instead. Tap it again to re-insert it.") : i18n("Tap the re-insert trigger once to activate the strip preview. Tap it again to deactivate.")

            SettingsSwitch {
                id: insertToggleSwitch

                checked: appSettings.scrollingDragInsertToggle
                accessibleName: alwaysReinsertSwitch.checked ? i18n("Toggle mode for floating instead of re-inserting") : i18n("Toggle mode for re-insert into strip")
                onToggled: function (newValue) {
                    appSettings.scrollingDragInsertToggle = newValue;
                }
            }
        }

        // Hold mode only, since toggle mode has no release to extend. With
        // "Always re-insert on drag" the grace extends the keep-out instead,
        // so the row inverts with the trigger row above.
        SettingsSeparator {
            enabled: !insertToggleSwitch.checked
        }

        TriggerGraceRow {
            title: i18n("Release grace period")
            searchAnchor: "scrollingReleaseGracePeriod"
            description: alwaysReinsertSwitch.checked ? i18n("How long a window stays out of the strip after the trigger is released, so a window dropped just after letting go of the trigger still floats. Helps when the trigger is a mouse button released with the drop. Set 0 to turn it off.") : i18n("How long the strip preview stays active after the re-insert trigger is released, so a window dropped just after letting go of the trigger still lands in the strip. Helps when the trigger is a mouse button released with the drop. Set 0 to turn it off.")
            accessibleName: alwaysReinsertSwitch.checked ? i18n("Release grace period for floating instead of re-inserting") : i18n("Release grace period for re-insert into strip")
            enabled: !insertToggleSwitch.checked
            minMs: root.settingsBridge.triggerGraceMsMin
            maxMs: root.settingsBridge.triggerGraceMsMax
            graceMs: appSettings.scrollingDragInsertGraceMs
            onGraceModified: value => appSettings.scrollingDragInsertGraceMs = value
        }
    }
}
