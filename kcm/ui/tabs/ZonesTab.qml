// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Zones tab - Consolidated appearance and behavior settings
 *
 * Merges the former Appearance and Behavior tabs into a unified card-based layout
 * for consistent UX across the KCM.
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    // Whether this tab is currently visible (for conditional tooltips)
    property bool isCurrentTab: false

    // Signals for color dialog interactions (handled by main.qml)
    signal requestHighlightColorDialog()
    signal requestInactiveColorDialog()
    signal requestBorderColorDialog()
    signal requestColorFileDialog()

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════════════════════════════════════════════════
        // APPEARANCE CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: appearanceCard.implicitHeight

            Kirigami.Card {
                id: appearanceCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Appearance")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    // Colors subsection
                    CheckBox {
                        id: useSystemColorsCheck
                        Kirigami.FormData.label: i18n("Color scheme:")
                        text: i18n("Use system accent color")
                        checked: kcm.useSystemColors
                        onToggled: kcm.useSystemColors = checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Highlight:")
                        visible: !useSystemColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        ColorButton {
                            color: kcm.highlightColor
                            onClicked: root.requestHighlightColorDialog()
                        }

                        Label {
                            text: kcm.highlightColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Inactive:")
                        visible: !useSystemColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        ColorButton {
                            color: kcm.inactiveColor
                            onClicked: root.requestInactiveColorDialog()
                        }

                        Label {
                            text: kcm.inactiveColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Border:")
                        visible: !useSystemColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        ColorButton {
                            color: kcm.borderColor
                            onClicked: root.requestBorderColorDialog()
                        }

                        Label {
                            text: kcm.borderColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Import colors:")
                        visible: !useSystemColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        Button {
                            text: i18n("From pywal")
                            icon.name: "color-management"
                            onClicked: kcm.loadColorsFromPywal()
                        }

                        Button {
                            text: i18n("From file...")
                            icon.name: "document-open"
                            onClicked: root.requestColorFileDialog()
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                    }

                    // Opacity subsection
                    RowLayout {
                        Kirigami.FormData.label: i18n("Active opacity:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: activeOpacitySlider
                            Layout.preferredWidth: root.constants.sliderPreferredWidth
                            from: 0
                            to: root.constants.opacitySliderMax
                            value: kcm.activeOpacity * root.constants.opacitySliderMax
                            onMoved: kcm.activeOpacity = value / root.constants.opacitySliderMax
                        }

                        Label {
                            text: Math.round(activeOpacitySlider.value) + "%"
                            Layout.preferredWidth: root.constants.sliderValueLabelWidth
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Inactive opacity:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: inactiveOpacitySlider
                            Layout.preferredWidth: root.constants.sliderPreferredWidth
                            from: 0
                            to: root.constants.opacitySliderMax
                            value: kcm.inactiveOpacity * root.constants.opacitySliderMax
                            onMoved: kcm.inactiveOpacity = value / root.constants.opacitySliderMax
                        }

                        Label {
                            text: Math.round(inactiveOpacitySlider.value) + "%"
                            Layout.preferredWidth: root.constants.sliderValueLabelWidth
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                    }

                    // Border subsection
                    RowLayout {
                        Kirigami.FormData.label: i18n("Border width:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: root.constants.borderWidthMax
                            value: kcm.borderWidth
                            onValueModified: kcm.borderWidth = value
                        }

                        Label {
                            text: i18n("px")
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Border radius:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: root.constants.borderRadiusMax
                            value: kcm.borderRadius
                            onValueModified: kcm.borderRadius = value
                        }

                        Label {
                            text: i18n("px")
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // EFFECTS CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: effectsCard.implicitHeight

            Kirigami.Card {
                id: effectsCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Effects")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        Kirigami.FormData.label: i18n("Blur:")
                        text: i18n("Enable blur behind zones")
                        checked: kcm.enableBlur
                        onToggled: kcm.enableBlur = checked
                    }

                    CheckBox {
                        id: shaderEffectsCheck
                        Kirigami.FormData.label: i18n("Shaders:")
                        text: i18n("Enable shader effects")
                        checked: kcm.enableShaderEffects
                        onToggled: kcm.enableShaderEffects = checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Shader FPS:")
                        enabled: shaderEffectsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: shaderFpsSlider
                            Layout.preferredWidth: root.constants.sliderPreferredWidth
                            from: 30
                            to: 144
                            stepSize: 1
                            value: kcm.shaderFrameRate
                            onMoved: kcm.shaderFrameRate = Math.round(value)
                        }

                        Label {
                            text: Math.round(shaderFpsSlider.value) + " fps"
                            Layout.preferredWidth: root.constants.sliderValueLabelWidth + 15
                        }
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Numbers:")
                        text: i18n("Show zone numbers")
                        checked: kcm.showZoneNumbers
                        onToggled: kcm.showZoneNumbers = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Animation:")
                        text: i18n("Flash zones when switching layouts")
                        checked: kcm.flashZonesOnSwitch
                        onToggled: kcm.flashZonesOnSwitch = checked
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // ON-SCREEN DISPLAY CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: osdCard.implicitHeight

            Kirigami.Card {
                id: osdCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("On-Screen Display")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        id: showOsdCheckbox
                        Kirigami.FormData.label: i18n("Layout switch:")
                        text: i18n("Show OSD when switching layouts")
                        checked: kcm.showOsdOnLayoutSwitch
                        onToggled: kcm.showOsdOnLayoutSwitch = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Keyboard navigation:")
                        text: i18n("Show OSD when using keyboard navigation")
                        checked: kcm.showNavigationOsd
                        onToggled: kcm.showNavigationOsd = checked
                    }

                    ComboBox {
                        id: osdStyleCombo
                        Kirigami.FormData.label: i18n("OSD style:")
                        enabled: showOsdCheckbox.checked || kcm.showNavigationOsd

                        readonly property int osdStyleNone: 0
                        readonly property int osdStyleText: 1
                        readonly property int osdStylePreview: 2

                        currentIndex: Math.max(0, kcm.osdStyle - osdStyleText)
                        model: [
                            i18n("Text only"),
                            i18n("Visual preview")
                        ]
                        onActivated: (index) => {
                            kcm.osdStyle = index + osdStyleText
                        }
                    }

                    // Visual preview OSD enhancements (only shown when preview style)
                    CheckBox {
                        Kirigami.FormData.label: i18n("OSD details:")
                        text: i18n("Show keyboard shortcuts (Meta+1, Meta+2, etc.)")
                        checked: kcm.showZoneShortcutsInOsd
                        onToggled: kcm.showZoneShortcutsInOsd = checked
                        enabled: osdStyleCombo.currentIndex === 1 && (showOsdCheckbox.checked || kcm.showNavigationOsd)
                    }

                    CheckBox {
                        text: i18n("Show context hints")
                        checked: kcm.showContextHintsInOsd
                        onToggled: kcm.showContextHintsInOsd = checked
                        enabled: osdStyleCombo.currentIndex === 1 && (showOsdCheckbox.checked || kcm.showNavigationOsd)
                    }

                    CheckBox {
                        text: i18n("Show app names in zone badges")
                        checked: kcm.showWindowTitlesInOsd
                        onToggled: kcm.showWindowTitlesInOsd = checked
                        enabled: osdStyleCombo.currentIndex === 1 && (showOsdCheckbox.checked || kcm.showNavigationOsd)
                    }

                    SpinBox {
                        Kirigami.FormData.label: i18n("Window count badges:")
                        from: 1
                        to: 10
                        value: kcm.osdWindowCountThreshold
                        onValueModified: kcm.osdWindowCountThreshold = value
                        enabled: osdStyleCombo.currentIndex === 1 && (showOsdCheckbox.checked || kcm.showNavigationOsd)
                        // Show badge when zone has more than X windows
                        textFromValue: function(value) {
                            return i18n("Show when > %1", value)
                        }
                    }
                }
            }
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

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Activation")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    ModifierCheckBoxes {
                        id: dragActivationModifiers
                        Kirigami.FormData.label: i18n("Zone activation:")
                        modifierValue: kcm.dragActivationModifier
                        tooltipEnabled: root.isCurrentTab
                        onValueModified: (value) => {
                            kcm.dragActivationModifier = value
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Multi-Zone Selection")
                    }

                    ModifierCheckBoxes {
                        id: multiZoneModifiers
                        Kirigami.FormData.label: i18n("Multi-zone modifier:")
                        modifierValue: kcm.multiZoneModifier
                        tooltipEnabled: root.isCurrentTab
                        ToolTip.text: i18n("Hold this modifier while dragging to span windows across multiple zones")
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

                            ToolTip.visible: hovered && root.isCurrentTab
                            ToolTip.text: i18n("Distance from zone edge for multi-zone selection")
                        }

                        Label {
                            text: i18n("px")
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // ZONE LAYOUT CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: zoneLayoutCard.implicitHeight

            Kirigami.Card {
                id: zoneLayoutCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Zone Layout")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
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
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // WINDOW BEHAVIOR CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: windowBehaviorCard.implicitHeight

            Kirigami.Card {
                id: windowBehaviorCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Window Behavior")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
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

                    CheckBox {
                        Kirigami.FormData.label: i18n("Session:")
                        text: i18n("Restore windows to their zones after login")
                        checked: kcm.restoreWindowsToZonesOnLogin
                        onToggled: kcm.restoreWindowsToZonesOnLogin = checked
                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("When enabled, windows return to their previous zones after logging in or restarting the session.")
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
        }
    }
}
