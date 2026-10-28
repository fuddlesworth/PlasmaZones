// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: root

    // Page-scoped Q_PROPERTY surface lives on the sub-controller; appSettings
    // references stay direct (those are Settings Q_PROPERTYs not wrapped here).
    readonly property var settingsBridge: settingsController.tilingBehaviorPage

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Triggers Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Triggers")
            searchAnchor: "triggers"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Always re-insert on drag")
                    searchAnchor: "alwaysReinsertOnDrag"
                    description: i18n("Dynamically insert dragged windows into the autotile stack at the cursor position without requiring a modifier key or mouse button. The trigger below then leaves a window floating instead.")

                    SettingsSwitch {
                        id: alwaysReinsertSwitch

                        checked: root.settingsBridge.alwaysReinsertIntoStack
                        accessibleName: i18n("Always re-insert into stack on drag")
                        onToggled: function (newValue) {
                            root.settingsBridge.alwaysReinsertIntoStack = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                // Dual purpose, like the scrolling twin and the snapping
                // activation triggers (#249): with "Always re-insert on drag"
                // on, the same triggers keep a drag OUT of the stack so the
                // window drops as a floating one (discussion #1162).
                SettingsRow {
                    title: alwaysReinsertSwitch.checked ? i18n("Hold to keep floating") : i18n("Hold to re-insert into stack")
                    searchAnchor: "holdToReinsert"
                    description: alwaysReinsertSwitch.checked ? i18n("Hold a modifier or mouse button while dragging a window to leave it floating instead of inserting it into the autotile stack.") : i18n("Hold a modifier or mouse button while dragging a window to dynamically insert it into the autotile stack at the cursor position")

                    ModifierAndMouseCheckBoxes {
                        width: TriggerLabels.editorPreferredWidth
                        acceptMode: acceptModeAll
                        // See the scrolling twin: the sentinel eats a stored
                        // slot that this list does not show.
                        reservedTriggerSlots: alwaysReinsertSwitch.checked ? 1 : 0
                        triggers: root.settingsBridge.autotileDragInsertTriggers
                        defaultTriggers: root.settingsBridge.defaultAutotileDragInsertTriggers
                        tooltipEnabled: false
                        onTriggersModified: triggers => {
                            root.settingsBridge.autotileDragInsertTriggers = triggers;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Toggle mode")
                    searchAnchor: "triggersToggleMode"
                    description: alwaysReinsertSwitch.checked ? i18n("Tap the trigger once to leave the dragged window floating. Tap it again to insert it.") : i18n("Tap the re-insert trigger once to activate the stack preview. Tap it again to deactivate.")

                    SettingsSwitch {
                        id: insertToggleSwitch

                        checked: appSettings.autotileDragInsertToggle
                        accessibleName: i18n("Toggle mode for re-insert into stack")
                        onToggled: function (newValue) {
                            appSettings.autotileDragInsertToggle = newValue;
                        }
                    }
                }

                // Plain hold mode only, as on the scrolling twin.
                SettingsSeparator {
                    enabled: !alwaysReinsertSwitch.checked && !insertToggleSwitch.checked
                }

                TriggerGraceRow {
                    title: i18n("Release grace period")
                    searchAnchor: "releaseGracePeriod"
                    description: i18n("How long the stack preview stays active after the re-insert trigger is released, so a window dropped just after letting go of the trigger still lands in the stack. Helps when the trigger is a mouse button released with the drop. Set 0 to turn it off.")
                    accessibleName: i18n("Release grace period for re-insert into stack")
                    enabled: !alwaysReinsertSwitch.checked && !insertToggleSwitch.checked
                    minMs: root.settingsBridge.triggerGraceMsMin
                    maxMs: root.settingsBridge.triggerGraceMsMax
                    graceMs: appSettings.autotileDragInsertGraceMs
                    onGraceModified: value => appSettings.autotileDragInsertGraceMs = value
                }
            }
        }

        // =================================================================
        // Window Handling Card (shared — also hosted on TilingSimplePage)
        // =================================================================
        TilingWindowHandlingCard {
            Layout.fillWidth: true
        }

        // =================================================================
        // Focus Card (shared — also hosted on TilingSimplePage)
        // =================================================================
        TilingFocusCard {
            Layout.fillWidth: true
        }
    }
}
