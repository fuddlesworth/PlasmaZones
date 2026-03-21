// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Per-monitor gaps and zone selector popup settings for the Snapping settings page.
 *
 * Contains the monitor selector, gaps card (zone padding, edge gaps, per-side gaps),
 * and the zone selector popup configuration.
 *
 * Required properties:
 *   - appSettings: the settings backend object
 *   - constants: root object providing paddingMax, screenAspectRatio,
 *                selectedSnappingScreenName, isPerScreenSnapping, hasSnappingOverrides,
 *                snappingSettingValue(), writeSnappingSetting()
 */
ColumnLayout {
    id: selectorRoot

    required property var appSettings
    // Controller with per-screen methods and screens list (defaults to appSettings for shared component compat)
    property var controller: appSettings
    required property var constants

    spacing: Kirigami.Units.largeSpacing

    MonitorSelectorSection {
        Layout.fillWidth: true
        appSettings: selectorRoot.controller
        featureEnabled: true
        selectedScreenName: selectorRoot.constants.selectedSnappingScreenName
        hasOverrides: selectorRoot.constants.hasSnappingOverrides
        onSelectedScreenNameChanged: selectorRoot.constants.selectedSnappingScreenName = selectedScreenName
        onResetClicked: selectorRoot.constants.clearSnappingOverrides()
    }

    // ═══════════════════════════════════════════════════════════════════════
    // GAPS CARD (per-monitor)
    // ═══════════════════════════════════════════════════════════════════════
    Item {
        Layout.fillWidth: true
        implicitHeight: gapsCard.implicitHeight

        Kirigami.Card {
            id: gapsCard

            anchors.fill: parent
            enabled: selectorRoot.appSettings.snappingEnabled

            header: Kirigami.Heading {
                level: 3
                text: i18n("Gaps")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: Kirigami.FormLayout {
                RowLayout {
                    Kirigami.FormData.label: i18n("Zone padding:")
                    spacing: Kirigami.Units.smallSpacing

                    SpinBox {
                        from: 0
                        to: selectorRoot.constants.paddingMax
                        value: selectorRoot.constants.snappingSettingValue("ZonePadding", selectorRoot.appSettings.zonePadding)
                        onValueModified: selectorRoot.constants.writeSnappingSetting("ZonePadding", value, function(v) {
                            selectorRoot.appSettings.zonePadding = v;
                        })
                        Accessible.name: i18n("Zone padding")
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
                        to: selectorRoot.constants.paddingMax
                        value: selectorRoot.constants.snappingSettingValue("OuterGap", selectorRoot.appSettings.outerGap)
                        enabled: !perSideCheck.checked
                        onValueModified: selectorRoot.constants.writeSnappingSetting("OuterGap", value, function(v) {
                            selectorRoot.appSettings.outerGap = v;
                        })
                        Accessible.name: i18n("Edge gap")
                    }

                    Label {
                        text: i18n("px")
                        visible: !perSideCheck.checked
                    }

                    CheckBox {
                        id: perSideCheck

                        text: i18n("Set per side")
                        checked: selectorRoot.constants.snappingSettingValue("UsePerSideOuterGap", selectorRoot.appSettings.usePerSideOuterGap)
                        onToggled: selectorRoot.constants.writeSnappingSetting("UsePerSideOuterGap", checked, function(v) {
                            selectorRoot.appSettings.usePerSideOuterGap = v;
                        })
                    }

                }

                GridLayout {
                    Kirigami.FormData.label: i18n("Per-side gaps:")
                    visible: perSideCheck.checked
                    columns: 6
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Top:")
                    }

                    SpinBox {
                        from: 0
                        to: selectorRoot.constants.paddingMax
                        value: selectorRoot.constants.snappingSettingValue("OuterGapTop", selectorRoot.appSettings.outerGapTop)
                        onValueModified: selectorRoot.constants.writeSnappingSetting("OuterGapTop", value, function(v) {
                            selectorRoot.appSettings.outerGapTop = v;
                        })
                        Accessible.name: i18nc("@label", "Top edge gap")
                    }

                    Label {
                        text: i18nc("@label", "px")
                    }

                    Label {
                        text: i18n("Bottom:")
                    }

                    SpinBox {
                        from: 0
                        to: selectorRoot.constants.paddingMax
                        value: selectorRoot.constants.snappingSettingValue("OuterGapBottom", selectorRoot.appSettings.outerGapBottom)
                        onValueModified: selectorRoot.constants.writeSnappingSetting("OuterGapBottom", value, function(v) {
                            selectorRoot.appSettings.outerGapBottom = v;
                        })
                        Accessible.name: i18nc("@label", "Bottom edge gap")
                    }

                    Label {
                        text: i18nc("@label", "px")
                    }

                    Label {
                        text: i18n("Left:")
                    }

                    SpinBox {
                        from: 0
                        to: selectorRoot.constants.paddingMax
                        value: selectorRoot.constants.snappingSettingValue("OuterGapLeft", selectorRoot.appSettings.outerGapLeft)
                        onValueModified: selectorRoot.constants.writeSnappingSetting("OuterGapLeft", value, function(v) {
                            selectorRoot.appSettings.outerGapLeft = v;
                        })
                        Accessible.name: i18nc("@label", "Left edge gap")
                    }

                    Label {
                        text: i18nc("@label", "px")
                    }

                    Label {
                        text: i18n("Right:")
                    }

                    SpinBox {
                        from: 0
                        to: selectorRoot.constants.paddingMax
                        value: selectorRoot.constants.snappingSettingValue("OuterGapRight", selectorRoot.appSettings.outerGapRight)
                        onValueModified: selectorRoot.constants.writeSnappingSetting("OuterGapRight", value, function(v) {
                            selectorRoot.appSettings.outerGapRight = v;
                        })
                        Accessible.name: i18nc("@label", "Right edge gap")
                    }

                    Label {
                        text: i18nc("@label", "px")
                    }

                }

            }

        }

    }

    // ═══════════════════════════════════════════════════════════════════════
    // ZONE SELECTOR (per-monitor popup configuration)
    // ═══════════════════════════════════════════════════════════════════════
    ZoneSelectorSection {
        Layout.fillWidth: true
        appSettings: selectorRoot.appSettings
        controller: selectorRoot.controller
        constants: selectorRoot.constants
        isCurrentTab: true
        screenAspectRatio: selectorRoot.constants.screenAspectRatio
    }

}
