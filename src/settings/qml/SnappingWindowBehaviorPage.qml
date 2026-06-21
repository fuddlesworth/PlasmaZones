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
        Item {
            Layout.fillWidth: true
            implicitHeight: snapAssistCard.implicitHeight

            SettingsCard {
                id: snapAssistCard

                anchors.fill: parent
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
                        description: i18n("Hold this modifier when releasing a window to show the picker for that snap only")
                        enabled: !snapAssistAlwaysSwitch.checked
                        opacity: enabled ? 1 : 0.4

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
                searchAnchor: "windowHandling"
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Re-snap on resolution change")
                        description: i18n("Move windows back to their zones after the screen resolution changes")

                        SettingsSwitch {
                            checked: appSettings.keepWindowsInZonesOnResolutionChange
                            accessibleName: i18n("Re-snap on resolution change")
                            onToggled: function (newValue) {
                                appSettings.keepWindowsInZonesOnResolutionChange = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Open new windows in the last-used zone")
                        description: i18n("Snap every newly opened window into whichever zone you most recently snapped a window into")

                        SettingsSwitch {
                            checked: appSettings.moveNewWindowsToLastZone
                            accessibleName: i18n("Open new windows in the last-used zone")
                            onToggled: function (newValue) {
                                appSettings.moveNewWindowsToLastZone = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Auto-assign new windows for all layouts")
                        description: i18n("Fill the first empty zone when a new window opens. When on, this overrides each layout's individual auto-assign toggle and applies to every layout.")

                        SettingsSwitch {
                            checked: appSettings.autoAssignAllLayouts
                            accessibleName: i18n("Auto-assign new windows for all layouts")
                            onToggled: function (newValue) {
                                appSettings.autoAssignAllLayouts = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Restore size on unsnap")
                        description: i18n("Return a window to its original size when dragged out of a zone")

                        SettingsSwitch {
                            checked: appSettings.restoreOriginalSizeOnUnsnap
                            accessibleName: i18n("Restore original size on unsnap")
                            onToggled: function (newValue) {
                                appSettings.restoreOriginalSizeOnUnsnap = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Restore windows to their previous zone")
                        description: i18n("When an app reopens, during the session or after a logout, return it to the zone it was last snapped in")

                        SettingsSwitch {
                            checked: appSettings.restoreWindowsToZonesOnLogin
                            accessibleName: i18n("Restore windows to their previous zone")
                            onToggled: function (newValue) {
                                appSettings.restoreWindowsToZonesOnLogin = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Restore unsnapped windows to their previous position")
                        description: i18n("When an unsnapped window reopens after a logout, it returns to the position and monitor it was on instead of wherever the compositor would place it. A per-window rule can override this either way, opting individual windows in or out.")

                        SettingsSwitch {
                            checked: appSettings.snappingRestoreFloatedWindowsOnLogin
                            accessibleName: i18n("Restore unsnapped windows to their previous position")
                            onToggled: function (newValue) {
                                appSettings.snappingRestoreFloatedWindowsOnLogin = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Unfloat to a zone when there is no previous zone")
                        description: i18n("When you unfloat a window that was never snapped, snap it to a fallback zone (last used, then first empty, then the first zone) instead of leaving it floating.")

                        SettingsSwitch {
                            checked: appSettings.snapUnfloatFallbackToZone
                            accessibleName: i18n("Unfloat to a zone when there is no previous zone")
                            onToggled: function (newValue) {
                                appSettings.snapUnfloatFallbackToZone = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Sticky windows")
                        description: i18n("How to handle windows that appear on all desktops")

                        WideComboBox {
                            id: stickyHandlingCombo

                            Accessible.name: i18n("Sticky windows")
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                {
                                    "text": i18n("Treat as normal"),
                                    "value": 0
                                },
                                {
                                    "text": i18n("Restore only"),
                                    "value": 1
                                },
                                {
                                    "text": i18n("Ignore all"),
                                    "value": 2
                                }
                            ]
                            currentIndex: Math.max(0, indexOfValue(appSettings.snappingStickyWindowHandling))
                            onActivated: appSettings.snappingStickyWindowHandling = currentValue
                        }
                    }
                }
            }
        }

        // =================================================================
        // FOCUS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: focusCard.implicitHeight

            SettingsCard {
                id: focusCard

                anchors.fill: parent
                headerText: i18n("Focus")
                searchAnchor: "focus"
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Focus new windows")
                        description: i18n("Focus a window when it is automatically placed into a zone on open")

                        SettingsSwitch {
                            checked: appSettings.snappingFocusNewWindows
                            accessibleName: i18n("Focus newly placed windows")
                            onToggled: function (newValue) {
                                appSettings.snappingFocusNewWindows = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Focus follows mouse")
                        description: i18n("Moving the mouse pointer over a snapped window gives it focus")

                        SettingsSwitch {
                            checked: appSettings.snappingFocusFollowsMouse
                            accessibleName: i18n("Snapped window focus follows mouse pointer")
                            onToggled: function (newValue) {
                                appSettings.snappingFocusFollowsMouse = newValue;
                            }
                        }
                    }
                }
            }
        }
    }
}
