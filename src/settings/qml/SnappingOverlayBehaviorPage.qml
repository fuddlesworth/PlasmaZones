// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Snapping → Overlay → Behavior. The drag-time zone overlay's activation:
// when it shows (triggers), multi-zone span, and which monitors it spans.
// Binds to the shared snappingBehaviorPage controller for the trigger lists
// (drag activation / zone span) that are not plain Settings Q_PROPERTYs.
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

        // =================================================================
        // TRIGGERS
        // =================================================================
        SettingsCard {
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

                // The activation trigger list and the Hold/Toggle controls
                // serve dual purpose (#249): when "Activate on every drag"
                // is on, the same triggers DEACTIVATE the overlay (hold to
                // hide; toggle to flip off the implicitly-on overlay).
                // resolveActivationActive in the runtime mirrors this with
                // an inversion gated on alwaysActiveOnDrag.
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

        // =================================================================
        // ZONE SPAN
        // =================================================================
        // Shared card — also hosted on SnappingSimplePage.
        SnappingZoneSpanCard {
            Layout.fillWidth: true
            settingsBridge: root.settingsBridge
            sliderPreferredWidth: root.sliderPreferredWidth
        }

        // =================================================================
        // DISPLAY
        // =================================================================
        SettingsCard {
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
            }
        }
    }
}
