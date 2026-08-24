// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

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
    // Explicit width for the SettingsRow control slot (a plain Row
    // positioner — Layout.* is ignored there), hoisted like the tiling
    // page's triggerPreferredWidth. Below ~640 px of row width the slot
    // clamp cuts the widget's right edge; shared pre-existing behaviour
    // with the tiling twin.
    readonly property int triggerPreferredWidth: Kirigami.Units.gridUnit * 16

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

        SettingsSeparator {
            enabled: !alwaysReinsertSwitch.checked
        }

        SettingsRow {
            title: i18n("Hold to re-insert into strip")
            searchAnchor: "scrollingHoldToReinsert"
            description: i18n("Hold a modifier or mouse button while dragging a window to insert it into the strip under the cursor. It becomes a new column, or stacks into the column it lands on.")
            enabled: !alwaysReinsertSwitch.checked

            ModifierAndMouseCheckBoxes {
                width: root.triggerPreferredWidth
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

        SettingsSeparator {
            enabled: !alwaysReinsertSwitch.checked
        }

        SettingsRow {
            title: i18n("Toggle mode")
            searchAnchor: "scrollingTriggersToggleMode"
            description: i18n("Tap the re-insert trigger once to activate the strip preview. Tap it again to deactivate.")
            enabled: !alwaysReinsertSwitch.checked

            SettingsSwitch {
                id: insertToggleSwitch

                checked: appSettings.scrollingDragInsertToggle
                accessibleName: i18n("Toggle mode for re-insert into strip")
                onToggled: function (newValue) {
                    appSettings.scrollingDragInsertToggle = newValue;
                }
            }
        }

        SettingsSeparator {
            enabled: !alwaysReinsertSwitch.checked && !insertToggleSwitch.checked
        }

        SettingsRow {
            title: i18n("Release grace period")
            searchAnchor: "scrollingReleaseGracePeriod"
            description: i18n("How long the strip preview stays active after the re-insert trigger is released, so a window dropped just after letting go of the trigger still lands in the strip. Helps when the trigger is a mouse button released with the drop. Set 0 to turn it off.")
            enabled: !alwaysReinsertSwitch.checked && !insertToggleSwitch.checked

            SettingsSpinBox {
                id: insertGraceSpin

                accessibleName: i18n("Release grace period for re-insert into strip")
                from: root.settingsBridge.triggerGraceMsMin
                to: root.settingsBridge.triggerGraceMsMax
                stepSize: 10
                unitText: i18nc("milliseconds unit suffix in a spin box", "ms")
                onValueModified: value => {
                    return appSettings.scrollingDragInsertGraceMs = value;
                }
                Binding on value {
                    value: appSettings.scrollingDragInsertGraceMs
                    when: !insertGraceSpin.editing
                    restoreMode: Binding.RestoreNone
                }
            }
        }
    }
}
