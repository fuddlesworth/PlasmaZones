// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Behavior tab - Activation, multi-zone selection, zone settings, and window behavior
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    // Whether this tab is currently visible (for conditional tooltips)
    property bool isCurrentTab: false

    clip: true
    contentWidth: availableWidth

    Kirigami.FormLayout {
        width: parent.width

        // Activation section
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Activation")
        }

        ModifierCheckBoxes {
            id: dragActivationModifiers
            Kirigami.FormData.label: i18n("Zone activation modifiers:")
            modifierValue: kcm.dragActivationModifier
            tooltipEnabled: root.isCurrentTab
            onValueModified: (value) => {
                kcm.dragActivationModifier = value
            }
        }

        // Multi-zone selection
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Multi-Zone Selection")
        }

        ModifierCheckBoxes {
            id: multiZoneModifiers
            Kirigami.FormData.label: i18n("Multi-zone modifier:")
            modifierValue: kcm.multiZoneModifier
            tooltipEnabled: root.isCurrentTab
            ToolTip.text: i18n("Hold this modifier combination while dragging to span windows across multiple zones")
            onValueModified: (value) => {
                kcm.multiZoneModifier = value
            }
        }

        CheckBox {
            Kirigami.FormData.label: i18n("Middle click:")
            text: i18n("Use middle mouse button to select multiple zones")
            checked: kcm.middleClickMultiZone
            onToggled: kcm.middleClickMultiZone = checked
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Edge threshold:")
            spacing: Kirigami.Units.smallSpacing

            SpinBox {
                id: adjacentThresholdSpinBox
                from: 0
                to: root.constants.thresholdMax
                value: kcm.adjacentThreshold
                onValueModified: kcm.adjacentThreshold = value
            }

            Label {
                text: i18n("px")
            }

            ToolTip.visible: adjacentThresholdSpinBox.hovered && root.isCurrentTab
            ToolTip.text: i18n("Distance from zone edge for multi-zone selection")
        }

        // Zone settings
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Zone Settings")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Zone padding:")
            spacing: Kirigami.Units.smallSpacing

            SpinBox {
                from: 0
                to: root.constants.thresholdMax
                value: kcm.zonePadding
                onValueModified: kcm.zonePadding = value
            }

            Label {
                text: i18n("px")
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Edge gap:")
            spacing: Kirigami.Units.smallSpacing

            SpinBox {
                from: 0
                to: root.constants.thresholdMax
                value: kcm.outerGap
                onValueModified: kcm.outerGap = value
            }

            Label {
                text: i18n("px")
            }
        }

        CheckBox {
            Kirigami.FormData.label: i18n("Display:")
            text: i18n("Show zones on all monitors while dragging")
            checked: kcm.showZonesOnAllMonitors
            onToggled: kcm.showZonesOnAllMonitors = checked
        }

        // Window behavior
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Window Behavior")
        }

        CheckBox {
            Kirigami.FormData.label: i18n("Resolution:")
            text: i18n("Keep windows in zones when resolution changes")
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

        ComboBox {
            id: stickyHandlingCombo
            Kirigami.FormData.label: i18n("Sticky windows:")
            textRole: "text"
            valueRole: "value"
            model: [
                { text: i18n("Treat as normal"), value: 0 },
                { text: i18n("Restore only"), value: 1 },
                { text: i18n("Ignore all"), value: 2 }
            ]
            currentIndex: indexForValue(kcm.stickyWindowHandling)
            onActivated: kcm.stickyWindowHandling = currentValue

            function indexForValue(value) {
                for (let i = 0; i < model.length; i++) {
                    if (model[i].value === value) return i
                }
                return 0
            }

            ToolTip.visible: hovered && root.isCurrentTab
            ToolTip.text: i18n("Sticky windows appear on all desktops. Choose how snapping should behave.")
        }
    }
}
