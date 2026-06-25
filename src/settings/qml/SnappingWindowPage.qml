// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Snapping → Window. One page for everything about snapped windows: behavior
// cards (Snap Assist, Window Handling, Focus) first, then appearance cards
// (Colors, Decorations, Borders, Gaps). Behavior and Appearance are sections
// here, not separate nav pages. Behavior binds snappingBehaviorPage; appearance
// binds snappingWindowAppearancePage (border/gap bounds) + the per-screen helper.
SettingsFlickable {
    id: root

    readonly property var behaviorBridge: settingsController.snappingBehaviorPage
    readonly property var appearanceBridge: settingsController.snappingWindowAppearancePage
    readonly property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16

    function snappingSettingValue(key, globalValue) {
        return snappingHelper.settingValue(key, globalValue);
    }
    function writeSnappingSetting(key, value, globalSetter) {
        snappingHelper.writeSetting(key, value, globalSetter);
    }

    contentHeight: content.implicitHeight
    clip: true

    PerScreenOverrideHelper {
        id: snappingHelper

        appSettings: settingsController
        selectedScreenName: settingsController.scopeScreenName
        getterMethod: "getPerScreenSnappingSettings"
        setterMethod: "setPerScreenSnappingSetting"
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════ BEHAVIOR ═══════════════════════════

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
                        searchAnchor: "alwaysShowAfterSnapping"
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
                        searchAnchor: "holdToEnable"
                        description: i18n("Hold this modifier when releasing a window to show the picker for that snap only")
                        enabled: !snapAssistAlwaysSwitch.checked

                        ModifierAndMouseCheckBoxes {
                            width: root.sliderPreferredWidth
                            allowMultiple: true
                            acceptMode: acceptModeAll
                            triggers: root.behaviorBridge.snapAssistTriggers
                            defaultTriggers: root.behaviorBridge.defaultSnapAssistTriggers
                            tooltipEnabled: false
                            onTriggersModified: triggers => {
                                root.behaviorBridge.snapAssistTriggers = triggers;
                            }
                        }
                    }
                }
            }
        }

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
                        searchAnchor: "reSnapOnResolutionChange"
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
                        searchAnchor: "openNewWindowsInLastUsedZone"
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
                        searchAnchor: "autoAssignNewWindowsAllLayouts"
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
                        searchAnchor: "restoreSizeOnUnsnap"
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
                        searchAnchor: "restoreWindowsToPreviousZone"
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
                        searchAnchor: "restoreUnsnappedWindowsPosition"
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
                        searchAnchor: "unfloatToZoneFallback"
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
                        searchAnchor: "stickyWindows"
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
                        searchAnchor: "focusNewWindows"
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
                        searchAnchor: "focusFollowsMouse"
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

        // ══════════════════════════ APPEARANCE ══════════════════════════

        // Colors / Decorations / Borders — shared with Tiling → Window via
        // AppearanceFacetCards. Global (not per-screen), so they read as "Global".
        AppearanceFacetCards {
            Layout.fillWidth: true

            useSystemBorderColors: appSettings.snappingUseSystemBorderColors
            activeBorderColor: appSettings.snappingBorderColor
            inactiveBorderColor: appSettings.snappingInactiveBorderColor
            activeColorDescription: i18n("Border color for the focused snapped window")
            inactiveColorDescription: i18n("Border color for unfocused snapped windows")
            hideTitleBars: appSettings.snappingHideTitleBars
            hideTitleBarsDescription: i18n("Remove window title bars while snapped, restored when floating")
            hideTitleBarsAccessibleName: i18n("Hide title bars on snapped windows")
            showBorder: appSettings.snappingShowBorder
            borderWidth: appSettings.snappingBorderWidth
            borderWidthMin: root.appearanceBridge.snappingBorderWidthMin
            borderWidthMax: root.appearanceBridge.snappingBorderWidthMax
            borderWidthDescription: i18n("Thickness of colored borders around snapped windows")
            borderRadius: appSettings.snappingBorderRadius
            borderRadiusMin: root.appearanceBridge.snappingBorderRadiusMin
            borderRadiusMax: root.appearanceBridge.snappingBorderRadiusMax

            onUseSystemBorderColorsToggled: checked => {
                return appSettings.snappingUseSystemBorderColors = checked;
            }
            onActiveBorderColorPicked: selectedColor => {
                return appSettings.snappingBorderColor = selectedColor;
            }
            onInactiveBorderColorPicked: selectedColor => {
                return appSettings.snappingInactiveBorderColor = selectedColor;
            }
            onHideTitleBarsToggled: checked => {
                return appSettings.snappingHideTitleBars = checked;
            }
            onShowBorderToggled: checked => {
                return appSettings.snappingShowBorder = checked;
            }
            onBorderWidthModified: value => {
                return appSettings.snappingBorderWidth = value;
            }
            onBorderRadiusModified: value => {
                return appSettings.snappingBorderRadius = value;
            }
        }

        GapsSettingsCard {
            Layout.fillWidth: true
            searchAnchor: "gaps"
            scopeEnabled: true
            scopeAppSettings: settingsController
            scopeHasOverridesMethod: "hasPerScreenSnappingSettings"
            scopeClearerMethod: "clearPerScreenSnappingSettings"
            primaryGapLabel: i18n("Inner gap")
            primaryGapDescription: i18n("Space between snapped windows")
            outerGapLabel: i18n("Outer gap")
            outerGapDescription: i18n("Space from screen edges to snapped windows")
            showSmartGaps: false
            gapMin: root.appearanceBridge.gapMin
            gapMax: root.appearanceBridge.gapMax
            primaryGapMin: root.appearanceBridge.zonePaddingMin
            primaryGapMax: root.appearanceBridge.zonePaddingMax
            primaryGapValue: root.snappingSettingValue("ZonePadding", appSettings.zonePadding)
            outerGapValue: root.snappingSettingValue("OuterGap", appSettings.outerGap)
            usePerSideOuterGap: root.snappingSettingValue("UsePerSideOuterGap", appSettings.usePerSideOuterGap)
            outerGapTopValue: root.snappingSettingValue("OuterGapTop", appSettings.outerGapTop)
            outerGapBottomValue: root.snappingSettingValue("OuterGapBottom", appSettings.outerGapBottom)
            outerGapLeftValue: root.snappingSettingValue("OuterGapLeft", appSettings.outerGapLeft)
            outerGapRightValue: root.snappingSettingValue("OuterGapRight", appSettings.outerGapRight)
            onPrimaryGapModified: value => {
                return root.writeSnappingSetting("ZonePadding", value, function (v) {
                    appSettings.zonePadding = v;
                });
            }
            onOuterGapModified: value => {
                return root.writeSnappingSetting("OuterGap", value, function (v) {
                    appSettings.outerGap = v;
                });
            }
            onUsePerSideOuterGapToggled: checked => {
                return root.writeSnappingSetting("UsePerSideOuterGap", checked, function (v) {
                    appSettings.usePerSideOuterGap = v;
                });
            }
            onOuterGapTopModified: value => {
                return root.writeSnappingSetting("OuterGapTop", value, function (v) {
                    appSettings.outerGapTop = v;
                });
            }
            onOuterGapBottomModified: value => {
                return root.writeSnappingSetting("OuterGapBottom", value, function (v) {
                    appSettings.outerGapBottom = v;
                });
            }
            onOuterGapLeftModified: value => {
                return root.writeSnappingSetting("OuterGapLeft", value, function (v) {
                    appSettings.outerGapLeft = v;
                });
            }
            onOuterGapRightModified: value => {
                return root.writeSnappingSetting("OuterGapRight", value, function (v) {
                    appSettings.outerGapRight = v;
                });
            }
        }
    }
}
