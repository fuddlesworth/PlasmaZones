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
    readonly property int triggerPreferredWidth: Kirigami.Units.gridUnit * 16

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
                    description: i18n("Dynamically insert dragged windows into the autotile stack at the cursor position without requiring a modifier key or mouse button")

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

                SettingsRow {
                    title: i18n("Hold to re-insert into stack")
                    searchAnchor: "holdToReinsert"
                    description: i18n("Hold a modifier or mouse button while dragging a window to dynamically insert it into the autotile stack at the cursor position")
                    enabled: !alwaysReinsertSwitch.checked

                    ModifierAndMouseCheckBoxes {
                        width: root.triggerPreferredWidth
                        allowMultiple: true
                        acceptMode: acceptModeAll
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
                    description: i18n("Tap the re-insert trigger once to activate the stack preview, tap again to deactivate it")
                    enabled: !alwaysReinsertSwitch.checked

                    SettingsSwitch {
                        checked: appSettings.autotileDragInsertToggle
                        accessibleName: i18n("Toggle mode for re-insert into stack")
                        onToggled: function (newValue) {
                            appSettings.autotileDragInsertToggle = newValue;
                        }
                    }
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
