// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

/**
 * @brief Snapping sub-KCM -- Zone selector popup, appearance, and behavior settings
 *
 * Standalone version of the former ZonesTab from the monolith KCM.
 * Color/font/file dialogs are handled inline.
 */
KCMUtils.SimpleKCM {
    // ═══════════════════════════════════════════════════════════════════════
    // DIALOGS
    // ═══════════════════════════════════════════════════════════════════════

    id: root

    // Capture the context property so child components can access it via root.kcmModule
    readonly property var kcmModule: kcm
    // Layout constants (previously from monolith's QtObject)
    readonly property int sliderPreferredWidth: 200
    readonly property int sliderValueLabelWidth: 40
    readonly property int opacitySliderMax: 100
    readonly property int borderWidthMax: 10
    readonly property int borderRadiusMax: 50
    readonly property int paddingMax: 50
    readonly property int thresholdMax: 100
    readonly property int zoneSelectorTriggerMax: 200
    readonly property int zoneSelectorPreviewWidthMin: 80
    readonly property int zoneSelectorPreviewWidthMax: 400
    readonly property int zoneSelectorPreviewHeightMin: 60
    readonly property int zoneSelectorPreviewHeightMax: 300
    readonly property int zoneSelectorGridColumnsMax: 10
    // Screen aspect ratio for preview calculations
    readonly property real screenAspectRatio: Screen.width > 0 && Screen.height > 0 ? (Screen.width / Screen.height) : (16 / 9)
    // Per-screen snapping gap/padding helper
    property alias selectedSnappingScreenName: snappingHelper.selectedScreenName
    readonly property alias isPerScreenSnapping: snappingHelper.isPerScreen
    readonly property alias hasSnappingOverrides: snappingHelper.hasOverrides

    function snappingSettingValue(key, globalValue) {
        return snappingHelper.settingValue(key, globalValue);
    }

    function writeSnappingSetting(key, value, globalSetter) {
        snappingHelper.writeSetting(key, value, globalSetter);
    }

    function clearSnappingOverrides() {
        snappingHelper.clearOverrides();
    }

    PerScreenOverrideHelper {
        id: snappingHelper

        kcm: root.kcmModule
        getterMethod: "getPerScreenSnappingSettings"
        setterMethod: "setPerScreenSnappingSetting"
        clearerMethod: "clearPerScreenSnappingSettings"
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // Enable toggle
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing

            Label {
                text: i18n("Enable Zone Snapping")
                font.bold: true
            }

            Item {
                Layout.fillWidth: true
            }

            Switch {
                checked: kcm.snappingEnabled
                onToggled: kcm.snappingEnabled = checked
                Accessible.name: i18n("Enable zone snapping")
            }

        }

        // ═══════════════════════════════════════════════════════════════════════
        // APPEARANCE + EFFECTS (extracted component)
        // ═══════════════════════════════════════════════════════════════════════
        AppearanceCard {
            Layout.fillWidth: true
            kcm: root.kcmModule
            constants: root
        }

        // ═══════════════════════════════════════════════════════════════════════
        // ACTIVATION CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: activationCard.implicitHeight

            Kirigami.Card {
                id: activationCard

                anchors.fill: parent
                enabled: kcm.snappingEnabled

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Activation")
                    padding: Kirigami.Units.smallSpacing
                }

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
                        checked: kcm.alwaysActivateOnDrag
                        onToggled: kcm.alwaysActivateOnDrag = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, the zone overlay appears on every window drag without requiring a modifier key or mouse button.")
                    }

                    ModifierAndMouseCheckBoxes {
                        id: dragActivationInput

                        Layout.fillWidth: true
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Hold to activate:")
                        enabled: !alwaysActivateCheck.checked
                        opacity: enabled ? 1 : 0.6
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: kcm.dragActivationTriggers
                        defaultTriggers: kcm.defaultDragActivationTriggers
                        tooltipEnabled: true
                        customTooltipText: i18n("Hold modifier or use mouse button to show zones while dragging. Add multiple triggers to activate with any of them.")
                        onTriggersModified: (triggers) => {
                            kcm.dragActivationTriggers = triggers;
                        }
                    }

                    CheckBox {
                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Toggle mode:")
                        enabled: !alwaysActivateCheck.checked
                        opacity: enabled ? 1 : 0.6
                        text: i18n("Tap trigger to toggle overlay")
                        checked: kcm.toggleActivation
                        onToggled: kcm.toggleActivation = checked
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
                        checked: kcm.zoneSpanEnabled
                        onToggled: kcm.zoneSpanEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, you can paint across multiple zones to snap a window to the combined area.")
                    }

                    ModifierAndMouseCheckBoxes {
                        Layout.fillWidth: true
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Modifier:")
                        enabled: zoneSpanEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: kcm.zoneSpanTriggers
                        defaultTriggers: kcm.defaultZoneSpanTriggers
                        tooltipEnabled: true
                        customTooltipText: i18n("Hold modifier or use mouse button while dragging to paint across zones. Add multiple triggers to activate with any of them.")
                        onTriggersModified: (triggers) => {
                            kcm.zoneSpanTriggers = triggers;
                        }
                    }

                    RowLayout {
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Edge threshold:")
                        enabled: zoneSpanEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            id: adjacentThresholdSpinBox

                            from: 5
                            to: root.thresholdMax
                            value: kcm.adjacentThreshold
                            onValueModified: kcm.adjacentThreshold = value
                            Accessible.name: i18n("Edge threshold")
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Distance from zone edge for multi-zone selection")
                        }

                        Label {
                            text: i18n("px")
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
                        checked: kcm.snapAssistFeatureEnabled
                        onToggled: kcm.snapAssistFeatureEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Show a window picker after snapping to fill remaining empty zones")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Behavior:")
                        text: i18n("Always show after snapping")
                        checked: kcm.snapAssistEnabled
                        onToggled: kcm.snapAssistEnabled = checked
                        enabled: snapAssistFeatureEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, a window picker appears after every snap. When disabled, hold the trigger below while dropping to show the picker for that snap only.")
                    }

                    ModifierAndMouseCheckBoxes {
                        Layout.fillWidth: true
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Hold to enable:")
                        enabled: snapAssistFeatureEnabledCheck.checked && !kcm.snapAssistEnabled
                        opacity: enabled ? 1 : 0.6
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: kcm.snapAssistTriggers
                        defaultTriggers: kcm.defaultSnapAssistTriggers
                        tooltipEnabled: true
                        customTooltipText: i18n("Hold this modifier or mouse button when releasing a window to show the picker for that snap only. Add multiple triggers to activate with any of them.")
                        onTriggersModified: (triggers) => {
                            kcm.snapAssistTriggers = triggers;
                        }
                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════════════
        // SNAPPING BEHAVIOR CARD (global)
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: snappingBehaviorCard.implicitHeight

            Kirigami.Card {
                id: snappingBehaviorCard

                anchors.fill: parent
                enabled: kcm.snappingEnabled

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Behavior")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        Kirigami.FormData.label: i18n("Display:")
                        text: i18n("Show zones on all monitors while dragging")
                        checked: kcm.showZonesOnAllMonitors
                        onToggled: kcm.showZonesOnAllMonitors = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Resolution:")
                        text: i18n("Re-snap windows to their zones after resolution changes")
                        checked: kcm.keepWindowsInZonesOnResolutionChange
                        onToggled: kcm.keepWindowsInZonesOnResolutionChange = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("New windows:")
                        text: i18n("Move new windows to their last used zone")
                        checked: kcm.moveNewWindowsToLastZone
                        onToggled: kcm.moveNewWindowsToLastZone = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Unsnapping:")
                        text: i18n("Restore original window size when unsnapping")
                        checked: kcm.restoreOriginalSizeOnUnsnap
                        onToggled: kcm.restoreOriginalSizeOnUnsnap = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Reopening:")
                        text: i18n("Restore windows to their previous zone")
                        checked: kcm.restoreWindowsToZonesOnLogin
                        onToggled: kcm.restoreWindowsToZonesOnLogin = checked
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
                        currentIndex: Math.max(0, indexOfValue(kcm.stickyWindowHandling))
                        onActivated: kcm.stickyWindowHandling = currentValue
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Sticky windows appear on all desktops. Choose how snapping should behave.")
                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════════════
        // PER-MONITOR GAPS + ZONE SELECTOR (extracted component)
        // ═══════════════════════════════════════════════════════════════════════
        ZoneSelectorCard {
            Layout.fillWidth: true
            kcm: root.kcmModule
            constants: root
        }

    }

}
