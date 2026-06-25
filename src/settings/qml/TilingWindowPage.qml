// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Tiling → Window. One page for everything about tiled windows: behavior cards
// (Triggers, Window Handling, Focus) first, then appearance cards (Colors,
// Decorations, Borders, Gaps). Behavior and Appearance are sections here, not
// separate nav pages — so a tiled-window setting is always one place, never a
// "which tab?" guess. Behavior binds tilingBehaviorPage; appearance binds
// tilingAppearancePage (border/gap bounds) + the per-screen gaps helper.
SettingsFlickable {
    id: root

    readonly property var behaviorBridge: settingsController.tilingBehaviorPage
    readonly property var appearanceBridge: settingsController.tilingAppearancePage
    readonly property int triggerPreferredWidth: Kirigami.Units.gridUnit * 16
    readonly property int gapMax: root.appearanceBridge.autotileGapMax

    // Per-screen override helper for the Gaps card (autotile per-monitor map).
    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }
    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    contentHeight: content.implicitHeight
    clip: true

    PerScreenOverrideHelper {
        id: psHelper

        appSettings: settingsController
        selectedScreenName: settingsController.scopeScreenName
        getterMethod: "getPerScreenAutotileSettings"
        setterMethod: "setPerScreenAutotileSetting"
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════ BEHAVIOR ═══════════════════════════

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Triggers")
            searchAnchor: "triggers"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Always re-insert on drag")
                    searchAnchor: "alwaysReinsertOnDrag"
                    description: i18n("Dynamically insert dragged windows into the autotile stack at the cursor position without requiring a modifier key or mouse button")

                    SettingsSwitch {
                        id: alwaysReinsertSwitch

                        checked: root.behaviorBridge.alwaysReinsertIntoStack
                        accessibleName: i18n("Always re-insert into stack on drag")
                        onToggled: function (newValue) {
                            root.behaviorBridge.alwaysReinsertIntoStack = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Hold to re-insert into stack")
                    searchAnchor: "holdToReinsert"
                    description: i18n("Hold a modifier or mouse button while dragging a window to dynamically insert it into the autotile stack at the cursor position")
                    enabled: !alwaysReinsertSwitch.checked

                    ModifierAndMouseCheckBoxes {
                        width: root.triggerPreferredWidth
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: root.behaviorBridge.autotileDragInsertTriggers
                        defaultTriggers: root.behaviorBridge.defaultAutotileDragInsertTriggers
                        tooltipEnabled: false
                        onTriggersModified: triggers => {
                            root.behaviorBridge.autotileDragInsertTriggers = triggers;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Toggle mode")
                    searchAnchor: "triggersToggleMode"
                    description: i18n("Tap the re-insert trigger once to activate the stack preview, tap again to deactivate it")
                    enabled: !alwaysReinsertSwitch.checked

                    SettingsSwitch {
                        checked: appSettings.autotileDragInsertToggle
                        accessibleName: i18n("Toggle mode for re-insert into stack")
                        onToggled: function (newValue) {
                            appSettings.autotileDragInsertToggle = newValue;
                        }
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Window Handling")
            searchAnchor: "windowHandling"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("New window placement")
                    searchAnchor: "newWindowPlacement"
                    description: i18n("Where newly opened windows appear in the tiling order")

                    ComboBox {
                        Layout.fillWidth: false
                        Accessible.name: i18n("New window placement")
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {
                                "text": i18n("After existing"),
                                "value": 0
                            },
                            {
                                "text": i18n("After focused"),
                                "value": 1
                            },
                            {
                                "text": i18n("As main window"),
                                "value": 2
                            }
                        ]
                        currentIndex: Math.max(0, indexOfValue(appSettings.autotileInsertPosition))
                        onActivated: appSettings.autotileInsertPosition = currentValue
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Respect minimum size")
                    searchAnchor: "respectMinimumSize"
                    description: i18n("Prevent windows from being resized below their minimum, which may leave gaps")

                    SettingsSwitch {
                        checked: appSettings.autotileRespectMinimumSize
                        accessibleName: i18n("Respect window minimum size")
                        onToggled: function (newValue) {
                            appSettings.autotileRespectMinimumSize = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Restore untiled windows to their previous position")
                    searchAnchor: "restoreUntiledWindowsPosition"
                    description: i18n("When an untiled (floated) window reopens after a logout, it returns to the position and monitor it was on instead of wherever the compositor would place it. A per-window rule can override this either way, opting individual windows in or out.")

                    SettingsSwitch {
                        checked: appSettings.autotileRestoreFloatedWindowsOnLogin
                        accessibleName: i18n("Restore untiled windows to their previous position")
                        onToggled: function (newValue) {
                            appSettings.autotileRestoreFloatedWindowsOnLogin = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Sticky windows")
                    searchAnchor: "stickyWindows"
                    description: i18n("How to handle windows that appear on all desktops")

                    WideComboBox {
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
                        currentIndex: Math.max(0, indexOfValue(appSettings.autotileStickyWindowHandling))
                        onActivated: appSettings.autotileStickyWindowHandling = currentValue
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Drag behavior")
                    searchAnchor: "dragBehavior"
                    description: i18n("Float converts a dragged tile to free-floating. Reorder keeps it tiled and swaps it into the drop slot.")

                    WideComboBox {
                        Accessible.name: i18n("Autotile drag behavior")
                        Accessible.description: i18n("Selects how dragging a tiled window on an autotile screen behaves: Float converts it to free-floating, Reorder keeps it tiled and swaps it into the drop slot.")
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {
                                "text": i18n("Float on drag"),
                                "value": 0
                            },
                            {
                                "text": i18n("Reorder on drag"),
                                "value": 1
                            }
                        ]
                        currentIndex: Math.max(0, indexOfValue(appSettings.autotileDragBehavior))
                        onActivated: appSettings.autotileDragBehavior = currentValue
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Overflow behavior")
                    searchAnchor: "overflowBehavior"
                    description: i18n("Float excess windows beyond the max-windows cap, or Unlimited to tile every window regardless of count.")

                    WideComboBox {
                        Accessible.name: i18n("Autotile overflow behavior")
                        Accessible.description: i18n("Selects how windows beyond the max-windows cap are handled: Float excess windows, or Unlimited to tile every window regardless of count.")
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {
                                "text": i18n("Float excess"),
                                "value": 0
                            },
                            {
                                "text": i18n("Unlimited"),
                                "value": 1
                            }
                        ]
                        currentIndex: Math.max(0, indexOfValue(appSettings.autotileOverflowBehavior))
                        onActivated: appSettings.autotileOverflowBehavior = currentValue
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Focus")
            searchAnchor: "focus"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Focus new windows")
                    searchAnchor: "focusNewWindows"
                    description: i18n("Focus a window when it opens")

                    SettingsSwitch {
                        checked: appSettings.autotileFocusNewWindows
                        accessibleName: i18n("Focus newly opened windows")
                        onToggled: function (newValue) {
                            appSettings.autotileFocusNewWindows = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Focus follows mouse")
                    searchAnchor: "focusFollowsMouse"
                    description: i18n("Moving the mouse pointer over a window gives it focus")

                    SettingsSwitch {
                        checked: appSettings.autotileFocusFollowsMouse
                        accessibleName: i18n("Focus follows mouse pointer")
                        onToggled: function (newValue) {
                            appSettings.autotileFocusFollowsMouse = newValue;
                        }
                    }
                }
            }
        }

        // ══════════════════════════ APPEARANCE ══════════════════════════

        // Colors / Decorations / Borders — shared with Snapping → Window via
        // AppearanceFacetCards. Global (not per-screen), so they read as "Global".
        AppearanceFacetCards {
            Layout.fillWidth: true

            useSystemBorderColors: appSettings.autotileUseSystemBorderColors
            activeBorderColor: appSettings.autotileBorderColor
            inactiveBorderColor: appSettings.autotileInactiveBorderColor
            hideTitleBars: appSettings.autotileHideTitleBars
            hideTitleBarsDescription: i18n("Remove window title bars while autotiled, restored when floating")
            showBorder: appSettings.autotileShowBorder
            borderWidth: appSettings.autotileBorderWidth
            borderWidthMin: root.appearanceBridge.autotileBorderWidthMin
            borderWidthMax: root.appearanceBridge.autotileBorderWidthMax
            borderRadius: appSettings.autotileBorderRadius
            borderRadiusMin: root.appearanceBridge.autotileBorderRadiusMin
            borderRadiusMax: root.appearanceBridge.autotileBorderRadiusMax

            onUseSystemBorderColorsToggled: checked => {
                return appSettings.autotileUseSystemBorderColors = checked;
            }
            onActiveBorderColorPicked: selectedColor => {
                return appSettings.autotileBorderColor = selectedColor;
            }
            onInactiveBorderColorPicked: selectedColor => {
                return appSettings.autotileInactiveBorderColor = selectedColor;
            }
            onHideTitleBarsToggled: checked => {
                return appSettings.autotileHideTitleBars = checked;
            }
            onShowBorderToggled: checked => {
                return appSettings.autotileShowBorder = checked;
            }
            onBorderWidthModified: value => {
                return appSettings.autotileBorderWidth = value;
            }
            onBorderRadiusModified: value => {
                return appSettings.autotileBorderRadius = value;
            }
        }

        GapsSettingsCard {
            Layout.fillWidth: true
            searchAnchor: "gaps"
            scopeEnabled: true
            scopeAppSettings: settingsController
            scopeHasOverridesMethod: "hasPerScreenAutotileGapsSettings"
            scopeClearerMethod: "clearPerScreenAutotileGapsSettings"
            gapMax: root.gapMax
            gapMin: root.appearanceBridge.autotileGapMin
            primaryGapMin: root.appearanceBridge.autotileInnerGapMin
            primaryGapMax: root.appearanceBridge.autotileInnerGapMax
            primaryGapValue: root.settingValue("InnerGap", appSettings.autotileInnerGap)
            outerGapValue: root.settingValue("OuterGap", appSettings.autotileOuterGap)
            usePerSideOuterGap: root.settingValue("UsePerSideOuterGap", appSettings.autotileUsePerSideOuterGap)
            smartGapsValue: root.settingValue("SmartGaps", appSettings.autotileSmartGaps)
            outerGapTopValue: root.settingValue("OuterGapTop", appSettings.autotileOuterGapTop)
            outerGapBottomValue: root.settingValue("OuterGapBottom", appSettings.autotileOuterGapBottom)
            outerGapLeftValue: root.settingValue("OuterGapLeft", appSettings.autotileOuterGapLeft)
            outerGapRightValue: root.settingValue("OuterGapRight", appSettings.autotileOuterGapRight)
            onPrimaryGapModified: value => {
                return root.writeSetting("InnerGap", value, function (v) {
                    appSettings.autotileInnerGap = v;
                });
            }
            onOuterGapModified: value => {
                return root.writeSetting("OuterGap", value, function (v) {
                    appSettings.autotileOuterGap = v;
                });
            }
            onUsePerSideOuterGapToggled: checked => {
                return root.writeSetting("UsePerSideOuterGap", checked, function (v) {
                    appSettings.autotileUsePerSideOuterGap = v;
                });
            }
            onOuterGapTopModified: value => {
                return root.writeSetting("OuterGapTop", value, function (v) {
                    appSettings.autotileOuterGapTop = v;
                });
            }
            onOuterGapBottomModified: value => {
                return root.writeSetting("OuterGapBottom", value, function (v) {
                    appSettings.autotileOuterGapBottom = v;
                });
            }
            onOuterGapLeftModified: value => {
                return root.writeSetting("OuterGapLeft", value, function (v) {
                    appSettings.autotileOuterGapLeft = v;
                });
            }
            onOuterGapRightModified: value => {
                return root.writeSetting("OuterGapRight", value, function (v) {
                    appSettings.autotileOuterGapRight = v;
                });
            }
            onSmartGapsToggled: checked => {
                return root.writeSetting("SmartGaps", checked, function (v) {
                    appSettings.autotileSmartGaps = v;
                });
            }
        }
    }
}
