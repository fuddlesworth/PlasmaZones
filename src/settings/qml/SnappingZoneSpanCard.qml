// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The Zone Span card, shared between the advanced Snapping →
 * Overlay → Behavior page and the simple-mode Snapping page. Trigger lists
 * are not plain Settings properties, so the hosting page passes its
 * snappingBehaviorPage bridge in.
 */
SettingsCard {
    id: card

    /// The snappingBehaviorPage controller (trigger lists live there).
    required property var settingsBridge
    property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16
    readonly property int thresholdMax: card.settingsBridge.adjacentThresholdMax

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
                width: card.sliderPreferredWidth
                allowMultiple: true
                acceptMode: acceptModeAll
                triggers: card.settingsBridge.zoneSpanTriggers
                defaultTriggers: card.settingsBridge.defaultZoneSpanTriggers
                tooltipEnabled: false
                onTriggersModified: triggers => {
                    card.settingsBridge.zoneSpanTriggers = triggers;
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
                id: edgeThresholdSpin

                accessibleName: i18n("Edge threshold")
                from: card.settingsBridge.adjacentThresholdMin
                to: card.thresholdMax
                onValueModified: value => {
                    return appSettings.adjacentThreshold = value;
                }
                // Feed value through a guarded Binding so a config change
                // keeps refreshing the control: a plain `value:` binding is
                // destroyed by SettingsSpinBox's own edit echo after the
                // first edit. RestoreNone + the focus gate keeps a live edit
                // from being clobbered.
                Binding on value {
                    value: appSettings.adjacentThreshold
                    when: !edgeThresholdSpin.editing
                    restoreMode: Binding.RestoreNone
                }
            }
        }
    }
}
