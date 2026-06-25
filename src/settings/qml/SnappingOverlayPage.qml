// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Snapping → Overlay. One page for the drag-time zone overlay: behavior cards
// (Triggers, Zone Span, Display) first, then the appearance cards (Colors,
// Opacity, Border, Zone Labels, Effects, Shaders) via SnappingOverlayAppearanceCards.
// Behavior binds snappingBehaviorPage; the appearance component binds
// snappingZonesPage + snappingEffectsPage itself.
SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.snappingBehaviorPage
    readonly property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16
    readonly property int thresholdMax: root.settingsBridge.adjacentThresholdMax

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════ BEHAVIOR ═══════════════════════════

        Item {
            Layout.fillWidth: true
            implicitHeight: triggersCard.implicitHeight

            SettingsCard {
                id: triggersCard

                anchors.fill: parent
                headerText: i18n("Triggers")
                searchAnchor: "triggers"
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Activate on every drag")
                        searchAnchor: "activateOnEveryDrag"
                        description: i18n("Show the zone overlay on every window drag without requiring a modifier key or mouse button")

                        SettingsSwitch {
                            id: alwaysActivateSwitch

                            checked: root.settingsBridge.alwaysActivateOnDrag
                            accessibleName: i18n("Activate on every window drag")
                            onToggled: function (newValue) {
                                root.settingsBridge.alwaysActivateOnDrag = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        readonly property string activeTitle: alwaysActivateSwitch.checked ? i18n("Hold to deactivate") : i18n("Hold to activate")
                        readonly property string activeDescription: alwaysActivateSwitch.checked ? i18n("Hold a modifier or mouse button while dragging to hide the zone overlay. Esc still cancels the drag entirely.") : i18n("Hold a modifier or mouse button to show zones while dragging")

                        title: activeTitle
                        searchAnchor: "holdToActivate"
                        description: activeDescription

                        ModifierAndMouseCheckBoxes {
                            id: dragActivationInput

                            width: root.sliderPreferredWidth
                            allowMultiple: true
                            acceptMode: acceptModeAll
                            triggers: root.settingsBridge.dragActivationTriggers
                            defaultTriggers: root.settingsBridge.defaultDragActivationTriggers
                            tooltipEnabled: false
                            onTriggersModified: triggers => {
                                root.settingsBridge.dragActivationTriggers = triggers;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        readonly property string activeDescription: alwaysActivateSwitch.checked ? i18n("Tap the trigger once to hide the overlay, tap again to show it") : i18n("Tap the activation trigger once to show the overlay, tap again to hide it")

                        title: i18n("Toggle mode")
                        searchAnchor: "triggersToggleMode"
                        description: activeDescription

                        SettingsSwitch {
                            checked: appSettings.toggleActivation
                            accessibleName: i18n("Toggle mode")
                            onToggled: function (newValue) {
                                appSettings.toggleActivation = newValue;
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: zoneSpanCard.implicitHeight

            SettingsCard {
                id: zoneSpanCard

                anchors.fill: parent
                headerText: i18n("Zone Span")
                searchAnchor: "zoneSpan"
                showToggle: true
                toggleChecked: appSettings.zoneSpanEnabled
                collapsible: true
                onToggleClicked: checked => {
                    return appSettings.zoneSpanEnabled = checked;
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Span modifier")
                        searchAnchor: "spanModifier"
                        description: i18n("Hold a modifier or mouse button while dragging to paint across zones")

                        ModifierAndMouseCheckBoxes {
                            width: root.sliderPreferredWidth
                            allowMultiple: true
                            acceptMode: acceptModeAll
                            triggers: root.settingsBridge.zoneSpanTriggers
                            defaultTriggers: root.settingsBridge.defaultZoneSpanTriggers
                            tooltipEnabled: false
                            onTriggersModified: triggers => {
                                root.settingsBridge.zoneSpanTriggers = triggers;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Toggle mode")
                        searchAnchor: "zoneSpanToggleMode"
                        description: i18n("Tap the span modifier once to start spanning, tap again to stop, instead of holding it")

                        SettingsSwitch {
                            checked: appSettings.zoneSpanToggleMode
                            accessibleName: i18n("Zone span toggle mode")
                            onToggled: function (newValue) {
                                appSettings.zoneSpanToggleMode = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Edge threshold")
                        searchAnchor: "edgeThreshold"
                        description: i18n("Distance from zone edge for multi-zone selection")

                        SettingsSpinBox {
                            from: root.settingsBridge.adjacentThresholdMin
                            to: root.thresholdMax
                            value: appSettings.adjacentThreshold
                            onValueModified: value => {
                                return appSettings.adjacentThreshold = value;
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: displayCard.implicitHeight

            SettingsCard {
                id: displayCard

                anchors.fill: parent
                headerText: i18n("Display")
                searchAnchor: "display"
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Show zones on all monitors")
                        searchAnchor: "showZonesOnAllMonitors"
                        description: i18n("Display zone overlays on every monitor while dragging a window")

                        SettingsSwitch {
                            checked: appSettings.showZonesOnAllMonitors
                            accessibleName: i18n("Show zones on all monitors")
                            onToggled: function (newValue) {
                                appSettings.showZonesOnAllMonitors = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Filter by aspect ratio")
                        searchAnchor: "filterByAspectRatio"
                        description: i18n("Only show layouts matching the current monitor's aspect ratio")

                        SettingsSwitch {
                            checked: appSettings.filterLayoutsByAspectRatio
                            accessibleName: i18n("Filter layouts by aspect ratio")
                            onToggled: function (newValue) {
                                appSettings.filterLayoutsByAspectRatio = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Zone numbers")
                        searchAnchor: "zoneNumbers"
                        description: i18n("Display a number label inside each zone")

                        SettingsSwitch {
                            checked: appSettings.showZoneNumbers
                            accessibleName: i18n("Show zone numbers")
                            onToggled: function (newValue) {
                                appSettings.showZoneNumbers = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Flash on layout switch")
                        searchAnchor: "flashOnLayoutSwitch"
                        description: i18n("Briefly flash zones when switching between layouts")

                        SettingsSwitch {
                            checked: appSettings.flashZonesOnSwitch
                            accessibleName: i18n("Flash zones on layout switch")
                            onToggled: function (newValue) {
                                appSettings.flashZonesOnSwitch = newValue;
                            }
                        }
                    }
                }
            }
        }

        // ══════════════════════════ APPEARANCE ══════════════════════════

        SnappingOverlayAppearanceCards {
            Layout.fillWidth: true
        }
    }
}
