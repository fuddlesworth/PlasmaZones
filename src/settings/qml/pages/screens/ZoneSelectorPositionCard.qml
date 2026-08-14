// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Position and trigger-distance card for a zone selector variant.
 *
 * Shared by the snapping selector (ZoneSelectorSection) and the scrolling
 * strip selector (ScrollingZoneSelectorSection): the host supplies the
 * resolved effective values, the write callbacks, and the per-screen scope
 * method names, so this card never touches variant-specific property names.
 */
Item {
    id: root

    required property var controller
    required property QtObject constants
    required property bool featureEnabled
    required property int effectivePosition
    required property int effectiveTriggerDistance
    required property string scopeHasOverridesMethod
    required property string scopeClearerMethod
    /// function(newPosition): writes the Position setting (scope-aware).
    required property var writePosition
    /// function(newDistance): writes the TriggerDistance setting.
    required property var writeTriggerDistance

    Layout.fillWidth: true
    implicitHeight: positionCard.implicitHeight

    SettingsCard {
        id: positionCard

        anchors.fill: parent
        enabled: root.featureEnabled
        headerText: i18n("Position and trigger")
        searchAnchor: "positionTrigger"
        collapsible: true
        scopeEnabled: true
        scopeAppSettings: root.controller
        scopeHasOverridesMethod: root.scopeHasOverridesMethod
        scopeClearerMethod: root.scopeClearerMethod

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            // Centered position picker with description
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: positionPicker.height + Kirigami.Units.gridUnit * 2

                PositionPicker {
                    id: positionPicker

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    position: root.effectivePosition
                    enabled: root.featureEnabled
                    onPositionSelected: function (newPosition) {
                        root.writePosition(newPosition);
                    }
                }

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: positionPicker.bottom
                    anchors.topMargin: Kirigami.Units.smallSpacing
                    text: i18n("Choose where the popup appears on screen")
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                }
            }

            SettingsSeparator {}

            // Trigger distance
            SettingsRow {
                title: i18n("Trigger distance")
                searchAnchor: "triggerDistance"
                description: i18n("How close to the screen edge before the popup appears")

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: triggerSlider

                        Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                        from: root.constants.zoneSelectorTriggerMin
                        to: root.constants.zoneSelectorTriggerMax
                        stepSize: 10
                        Accessible.name: i18n("Trigger distance")
                        onMoved: root.writeTriggerDistance(value)

                        Binding on value {
                            value: root.effectiveTriggerDistance
                            when: !triggerSlider.pressed
                            restoreMode: Binding.RestoreNone
                        }
                    }

                    Label {
                        text: i18nc("pixel-unit suffix after slider value", "%1 px", root.effectiveTriggerDistance)
                        Layout.preferredWidth: root.constants.sliderValueLabelWidth + Kirigami.Units.largeSpacing
                        horizontalAlignment: Text.AlignRight
                        font: Kirigami.Theme.fixedWidthFont
                    }
                }
            }
        }
    }
}
