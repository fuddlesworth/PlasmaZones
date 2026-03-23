// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16
    readonly property int thresholdMax: 500

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =====================================================================
        // ACTIVATION
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: activationCard.implicitHeight

            SettingsCard {
                id: activationCard

                anchors.fill: parent
                headerText: i18n("Activation")
                collapsible: true

                contentItem: Kirigami.FormLayout {
                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Triggers")
                    }

                    CheckBox {
                        id: alwaysActivateCheck

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Zone activation:")
                        text: i18n("Activate on every window drag")
                        checked: settingsController.alwaysActivateOnDrag
                        onToggled: settingsController.alwaysActivateOnDrag = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, the zone overlay appears on every window drag without requiring a modifier key or mouse button.")
                    }

                    ModifierAndMouseCheckBoxes {
                        id: dragActivationInput

                        Layout.fillWidth: true
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Hold to activate:")
                        enabled: !alwaysActivateCheck.checked
                        opacity: enabled ? 1 : 0.4
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: settingsController.dragActivationTriggers
                        defaultTriggers: settingsController.defaultDragActivationTriggers
                        tooltipEnabled: true
                        customTooltipText: i18n("Hold modifier or use mouse button to show zones while dragging. Add multiple triggers to activate with any of them.")
                        onTriggersModified: (triggers) => {
                            settingsController.dragActivationTriggers = triggers;
                        }
                    }

                    CheckBox {
                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Toggle mode:")
                        enabled: !alwaysActivateCheck.checked
                        opacity: enabled ? 1 : 0.4
                        text: i18n("Tap trigger to toggle overlay")
                        checked: appSettings.toggleActivation
                        onToggled: appSettings.toggleActivation = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, press the activation trigger once to show the overlay, press again to hide it. When disabled, hold the trigger to show.")
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Zone Span")
                    }

                    CheckBox {
                        id: zoneSpanEnabledCheck

                        Kirigami.FormData.label: i18n("Paint-to-span:")
                        text: i18n("Enable zone spanning")
                        checked: appSettings.zoneSpanEnabled
                        onToggled: appSettings.zoneSpanEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, you can paint across multiple zones to snap a window to the combined area.")
                    }

                    ModifierAndMouseCheckBoxes {
                        Layout.fillWidth: true
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Modifier:")
                        enabled: zoneSpanEnabledCheck.checked
                        opacity: enabled ? 1 : 0.4
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: settingsController.zoneSpanTriggers
                        defaultTriggers: settingsController.defaultZoneSpanTriggers
                        tooltipEnabled: true
                        customTooltipText: i18n("Hold modifier or use mouse button while dragging to paint across zones. Add multiple triggers to activate with any of them.")
                        onTriggersModified: (triggers) => {
                            settingsController.zoneSpanTriggers = triggers;
                        }
                    }

                    SettingsSpinBox {
                        Layout.preferredWidth: root.sliderPreferredWidth
                        formLabel: i18n("Edge threshold:")
                        enabled: zoneSpanEnabledCheck.checked
                        opacity: enabled ? 1 : 0.4
                        from: 5
                        to: root.thresholdMax
                        value: appSettings.adjacentThreshold
                        tooltipText: i18n("Distance from zone edge for multi-zone selection")
                        onValueModified: (value) => {
                            return appSettings.adjacentThreshold = value;
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Snap Assist")
                    }

                    CheckBox {
                        id: snapAssistFeatureEnabledCheck

                        Kirigami.FormData.label: i18n("Window picker:")
                        text: i18n("Enable snap assist")
                        checked: appSettings.snapAssistFeatureEnabled
                        onToggled: appSettings.snapAssistFeatureEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Show a window picker after snapping to fill remaining empty zones")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Behavior:")
                        text: i18n("Always show after snapping")
                        checked: appSettings.snapAssistEnabled
                        onToggled: appSettings.snapAssistEnabled = checked
                        enabled: snapAssistFeatureEnabledCheck.checked
                        opacity: enabled ? 1 : 0.4
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, a window picker appears after every snap. When disabled, hold the trigger below while dropping to show the picker for that snap only.")
                    }

                    ModifierAndMouseCheckBoxes {
                        Layout.fillWidth: true
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Hold to enable:")
                        enabled: snapAssistFeatureEnabledCheck.checked && !appSettings.snapAssistEnabled
                        opacity: enabled ? 1 : 0.4
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: settingsController.snapAssistTriggers
                        defaultTriggers: settingsController.defaultSnapAssistTriggers
                        tooltipEnabled: true
                        customTooltipText: i18n("Hold this modifier or mouse button when releasing a window to show the picker for that snap only. Add multiple triggers to activate with any of them.")
                        onTriggersModified: (triggers) => {
                            settingsController.snapAssistTriggers = triggers;
                        }
                    }

                }

            }

        }

        // =====================================================================
        // BEHAVIOR
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: behaviorCard.implicitHeight

            SettingsCard {
                id: behaviorCard

                anchors.fill: parent
                headerText: i18n("Behavior")
                collapsible: true

                contentItem: Kirigami.FormLayout {
                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Display")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Multi-monitor:")
                        text: i18n("Show zones on all monitors while dragging")
                        checked: appSettings.showZonesOnAllMonitors
                        onToggled: appSettings.showZonesOnAllMonitors = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Aspect ratio filter:")
                        text: i18n("Only show layouts matching this monitor's aspect ratio")
                        checked: appSettings.filterLayoutsByAspectRatio
                        onToggled: appSettings.filterLayoutsByAspectRatio = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, the zone selector, layout picker, and cycle shortcuts only show layouts designed for the current monitor's aspect ratio. Layouts tagged as 'Any' always appear.")
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Window Handling")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Resolution:")
                        text: i18n("Re-snap windows to their zones after resolution changes")
                        checked: appSettings.keepWindowsInZonesOnResolutionChange
                        onToggled: appSettings.keepWindowsInZonesOnResolutionChange = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("New windows:")
                        text: i18n("Move new windows to their last used zone")
                        checked: appSettings.moveNewWindowsToLastZone
                        onToggled: appSettings.moveNewWindowsToLastZone = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Unsnapping:")
                        text: i18n("Restore original window size when unsnapping")
                        checked: appSettings.restoreOriginalSizeOnUnsnap
                        onToggled: appSettings.restoreOriginalSizeOnUnsnap = checked
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Session Restore")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Reopening:")
                        text: i18n("Restore windows to their previous zone")
                        checked: appSettings.restoreWindowsToZonesOnLogin
                        onToggled: appSettings.restoreWindowsToZonesOnLogin = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, windows return to their previous zone when reopened, including after login or session restart.")
                    }

                    WideComboBox {
                        id: stickyHandlingCombo

                        Kirigami.FormData.label: i18n("Sticky windows:")
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
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Sticky windows appear on all desktops. Choose how snapping should behave.")
                    }

                }

            }

        }

    }

}
