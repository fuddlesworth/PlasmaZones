// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Snapping → Window → Behavior. How snapped WINDOWS behave: snap-assist (the
// fill-empty-zones picker) and window-handling (restore on login, re-snap on
// resolution change, sticky handling, …). Binds to the shared
// snappingBehaviorPage controller for the snap-assist trigger list.
SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.snappingBehaviorPage
    readonly property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // SNAP ASSIST
        // =================================================================
        SettingsCard {
            headerText: i18n("Snap Assist")
            searchAnchor: "snapAssist"
            showToggle: true
            toggleChecked: appSettings.snapAssistFeatureEnabled
            collapsible: true
            onToggleClicked: checked => {
                return appSettings.snapAssistFeatureEnabled = checked;
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Always show after snapping")
                    searchAnchor: "alwaysShowAfterSnapping"
                    description: i18n("Show a window picker after every snap to fill remaining empty zones")

                    SettingsSwitch {
                        id: snapAssistAlwaysSwitch

                        checked: appSettings.snapAssistEnabled
                        accessibleName: i18n("Always show after snapping")
                        onToggled: function (newValue) {
                            appSettings.snapAssistEnabled = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Hold to enable")
                    searchAnchor: "holdToEnable"
                    description: i18n("Hold this modifier when releasing a window to show the picker for that snap only")
                    enabled: !snapAssistAlwaysSwitch.checked

                    ModifierAndMouseCheckBoxes {
                        width: root.sliderPreferredWidth
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: root.settingsBridge.snapAssistTriggers
                        defaultTriggers: root.settingsBridge.defaultSnapAssistTriggers
                        tooltipEnabled: false
                        onTriggersModified: triggers => {
                            root.settingsBridge.snapAssistTriggers = triggers;
                        }
                    }
                }
            }
        }

        // =================================================================
        // WINDOW HANDLING (shared card — also hosted on SnappingSimplePage)
        // =================================================================
        SnappingWindowHandlingCard {
            Layout.fillWidth: true
        }

        // =================================================================
        // FOCUS (shared card — also hosted on SnappingSimplePage)
        // =================================================================
        SnappingFocusCard {
            Layout.fillWidth: true
        }
    }
}
