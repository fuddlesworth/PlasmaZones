// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16
    readonly property int thresholdMax: settingsController.adjacentThresholdMax

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // TRIGGERS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: triggersCard.implicitHeight

            SettingsCard {
                id: triggersCard

                anchors.fill: parent
                headerText: i18n("Triggers")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Activate on every drag")
                        description: i18n("Show the zone overlay on every window drag without requiring a modifier key or mouse button")

                        SettingsSwitch {
                            id: alwaysActivateSwitch

                            checked: settingsController.alwaysActivateOnDrag
                            accessibleName: i18n("Activate on every window drag")
                            onToggled: settingsController.alwaysActivateOnDrag = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Hold to activate")
                        description: i18n("Hold a modifier or mouse button to show zones while dragging")
                        enabled: !alwaysActivateSwitch.checked
                        opacity: enabled ? 1 : 0.4

                        ModifierAndMouseCheckBoxes {
                            id: dragActivationInput

                            width: Math.min(root.sliderPreferredWidth, Kirigami.Units.gridUnit * 16)
                            allowMultiple: true
                            acceptMode: acceptModeAll
                            triggers: settingsController.dragActivationTriggers
                            defaultTriggers: settingsController.defaultDragActivationTriggers
                            tooltipEnabled: false
                            onTriggersModified: (triggers) => {
                                settingsController.dragActivationTriggers = triggers;
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Toggle mode")
                        description: i18n("Tap the activation trigger once to show the overlay, tap again to hide it")
                        enabled: !alwaysActivateSwitch.checked
                        opacity: enabled ? 1 : 0.4

                        SettingsSwitch {
                            checked: appSettings.toggleActivation
                            accessibleName: i18n("Toggle mode")
                            onToggled: appSettings.toggleActivation = checked
                        }

                    }

                }

            }

        }

        // =================================================================
        // ZONE SPAN
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: zoneSpanCard.implicitHeight

            SettingsCard {
                id: zoneSpanCard

                anchors.fill: parent
                headerText: i18n("Zone Span")
                showToggle: true
                toggleChecked: appSettings.zoneSpanEnabled
                collapsible: true
                onToggleClicked: (checked) => {
                    return appSettings.zoneSpanEnabled = checked;
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Span modifier")
                        description: i18n("Hold a modifier or mouse button while dragging to paint across zones")

                        ModifierAndMouseCheckBoxes {
                            width: Math.min(root.sliderPreferredWidth, Kirigami.Units.gridUnit * 16)
                            allowMultiple: true
                            acceptMode: acceptModeAll
                            triggers: settingsController.zoneSpanTriggers
                            defaultTriggers: settingsController.defaultZoneSpanTriggers
                            tooltipEnabled: false
                            onTriggersModified: (triggers) => {
                                settingsController.zoneSpanTriggers = triggers;
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Edge threshold")
                        description: i18n("Distance from zone edge for multi-zone selection")

                        SettingsSpinBox {
                            from: settingsController.adjacentThresholdMin
                            to: root.thresholdMax
                            value: appSettings.adjacentThreshold
                            onValueModified: (value) => {
                                return appSettings.adjacentThreshold = value;
                            }
                        }

                    }

                }

            }

        }

        // =================================================================
        // SNAP ASSIST
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: snapAssistCard.implicitHeight

            SettingsCard {
                id: snapAssistCard

                anchors.fill: parent
                headerText: i18n("Snap Assist")
                showToggle: true
                toggleChecked: appSettings.snapAssistFeatureEnabled
                collapsible: true
                onToggleClicked: (checked) => {
                    return appSettings.snapAssistFeatureEnabled = checked;
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Always show after snapping")
                        description: i18n("Show a window picker after every snap to fill remaining empty zones")

                        SettingsSwitch {
                            id: snapAssistAlwaysSwitch

                            checked: appSettings.snapAssistEnabled
                            accessibleName: i18n("Always show after snapping")
                            onToggled: appSettings.snapAssistEnabled = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Hold to enable")
                        description: i18n("Hold this modifier when releasing a window to show the picker for that snap only")
                        enabled: !snapAssistAlwaysSwitch.checked
                        opacity: enabled ? 1 : 0.4

                        ModifierAndMouseCheckBoxes {
                            width: Math.min(root.sliderPreferredWidth, Kirigami.Units.gridUnit * 16)
                            allowMultiple: true
                            acceptMode: acceptModeAll
                            triggers: settingsController.snapAssistTriggers
                            defaultTriggers: settingsController.defaultSnapAssistTriggers
                            tooltipEnabled: false
                            onTriggersModified: (triggers) => {
                                settingsController.snapAssistTriggers = triggers;
                            }
                        }

                    }

                }

            }

        }

        // =================================================================
        // DISPLAY
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: displayCard.implicitHeight

            SettingsCard {
                id: displayCard

                anchors.fill: parent
                headerText: i18n("Display")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Show zones on all monitors")
                        description: i18n("Display zone overlays on every monitor while dragging a window")

                        SettingsSwitch {
                            checked: appSettings.showZonesOnAllMonitors
                            accessibleName: i18n("Show zones on all monitors")
                            onToggled: appSettings.showZonesOnAllMonitors = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Filter by aspect ratio")
                        description: i18n("Only show layouts matching the current monitor's aspect ratio")

                        SettingsSwitch {
                            checked: appSettings.filterLayoutsByAspectRatio
                            accessibleName: i18n("Filter layouts by aspect ratio")
                            onToggled: appSettings.filterLayoutsByAspectRatio = checked
                        }

                    }

                }

            }

        }

        // =================================================================
        // WINDOW HANDLING
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: windowHandlingCard.implicitHeight

            SettingsCard {
                id: windowHandlingCard

                anchors.fill: parent
                headerText: i18n("Window Handling")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Re-snap on resolution change")
                        description: i18n("Move windows back to their zones after the screen resolution changes")

                        SettingsSwitch {
                            checked: appSettings.keepWindowsInZonesOnResolutionChange
                            accessibleName: i18n("Re-snap on resolution change")
                            onToggled: appSettings.keepWindowsInZonesOnResolutionChange = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("New windows to last zone")
                        description: i18n("Automatically move newly opened windows to the zone they last occupied")

                        SettingsSwitch {
                            checked: appSettings.moveNewWindowsToLastZone
                            accessibleName: i18n("Move new windows to last zone")
                            onToggled: appSettings.moveNewWindowsToLastZone = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Restore size on unsnap")
                        description: i18n("Return a window to its original size when dragged out of a zone")

                        SettingsSwitch {
                            checked: appSettings.restoreOriginalSizeOnUnsnap
                            accessibleName: i18n("Restore original size on unsnap")
                            onToggled: appSettings.restoreOriginalSizeOnUnsnap = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Restore zones on login")
                        description: i18n("Return windows to their previous zone when reopened or after a session restart")

                        SettingsSwitch {
                            checked: appSettings.restoreWindowsToZonesOnLogin
                            accessibleName: i18n("Restore zones on login")
                            onToggled: appSettings.restoreWindowsToZonesOnLogin = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Sticky windows")
                        description: i18n("How to handle windows that appear on all desktops")

                        WideComboBox {
                            id: stickyHandlingCombo

                            textRole: "text"
                            valueRole: "value"
                            model: [{
                                "text": i18n("Treat as normal"),
                                "value": 0
                            }, {
                                "text": i18n("Restore only"),
                                "value": 1
                            }, {
                                "text": i18n("Ignore all"),
                                "value": 2
                            }]
                            currentIndex: Math.max(0, indexOfValue(appSettings.stickyWindowHandling))
                            onActivated: appSettings.stickyWindowHandling = currentValue
                        }

                    }

                }

            }

        }

    }

}
