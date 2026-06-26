// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Slim "Window Appearance" page. The window border and title-bar appearance is
// resolved entirely from the managed baseline appearance WindowRule (the
// catch-all, lowest-priority rule the daemon seeds). This page is a friendly
// editor for that single rule: it reads and writes the rule's actions through
// the Window Rules controller. Per-window overrides are ordinary higher-priority
// rules edited on the Window Rules page (the link at the bottom).
SettingsFlickable {
    id: root

    // Bounds + the baseline rule id.
    readonly property var bounds: settingsController.windowAppearancePage
    // The Window Rules controller owns the rule model + the daemon round-trip.
    readonly property var ruleController: settingsController.windowRulesPage
    readonly property string baselineId: bounds.baselineRuleId

    // Action wire strings (mirrors PhosphorWindowRules::ActionType).
    readonly property string actBorderVisible: "setBorderVisible"
    readonly property string actBorderWidth: "setBorderWidth"
    readonly property string actBorderRadius: "setBorderRadius"
    readonly property string actBorderColor: "setBorderColor"
    readonly property string actHideTitleBar: "setHideTitleBar"
    // Gap action wire strings (mirrors PhosphorWindowRules::ActionType). The
    // shared inner/outer gap model is rule-backed too, so the Gaps card edits the
    // same baseline rule as the border controls.
    readonly property string actInnerGap: "setInnerGap"
    readonly property string actOuterGap: "setOuterGap"
    readonly property string actUsePerSideOuterGap: "setUsePerSideOuterGap"
    readonly property string actOuterGapTop: "setOuterGapTop"
    readonly property string actOuterGapBottom: "setOuterGapBottom"
    readonly property string actOuterGapLeft: "setOuterGapLeft"
    readonly property string actOuterGapRight: "setOuterGapRight"
    // "Follow the system accent" sentinel (PhosphorWindowRules::BorderColorToken::Accent).
    // Writing it records intent; the effect-side push that resolves the sentinel
    // to the live accent colour is a pending follow-up (see the daemon's
    // makeBaselineAppearanceRule note), so for now it persists as a stored token.
    readonly property string accentToken: "accent"
    // Concrete fallback colour written when the user turns the accent toggle off
    // (matches the daemon's seeded baseline default — KDE accent blue, opaque).
    readonly property string defaultBorderHex: "#FF3DAEE9"

    // Bumped whenever the rule changes (daemon reload or a local write) so the
    // function-call value bindings below re-evaluate against the fresh rule.
    property int reloadTick: 0

    contentHeight: content.implicitHeight
    clip: true

    // Read one action param from the baseline rule, or @p fallback if absent.
    function actionValue(typeWire, paramKey, fallback) {
        const rule = ruleController.ruleJson(baselineId);
        const actions = rule.actions || [];
        for (var i = 0; i < actions.length; ++i) {
            if (actions[i].type === typeWire) {
                const v = actions[i][paramKey];
                return v === undefined ? fallback : v;
            }
        }
        return fallback;
    }

    // Write one or more param values onto the baseline rule's action of the
    // given type (creating the action if it is somehow absent), then push the
    // updated rule back through the controller. @p params is a plain object of
    // { paramKey: value } pairs.
    function writeAction(typeWire, params) {
        const rule = ruleController.ruleJson(baselineId);
        if (!rule || !rule.id) {
            return;
        }
        const actions = (rule.actions || []).slice();
        var found = false;
        for (var i = 0; i < actions.length; ++i) {
            if (actions[i].type === typeWire) {
                var updated = Object.assign({}, actions[i]);
                for (var k in params) {
                    updated[k] = params[k];
                }
                actions[i] = updated;
                found = true;
                break;
            }
        }
        if (!found) {
            var created = {
                "type": typeWire
            };
            for (var k2 in params) {
                created[k2] = params[k2];
            }
            actions.push(created);
        }
        rule.actions = actions;
        ruleController.updateRuleFromJson(rule);
        root.reloadTick++;
    }

    // Always emit the full 8-digit #AARRGGBB form so the stored value matches
    // what the effect resolves (it parses #AARRGGBB / #RRGGBB / #RGB).
    function colorToHex(c) {
        function pad(v) {
            return Math.round(v * 255).toString(16).padStart(2, '0');
        }
        return ("#" + pad(c.a) + pad(c.r) + pad(c.g) + pad(c.b)).toUpperCase();
    }

    Connections {
        target: root.ruleController
        function onRulesLoaded() {
            root.reloadTick++;
        }
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Borders Card — the master "show border" toggle plus width/radius.
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Borders")
            searchAnchor: "borders"
            showToggle: true
            toggleChecked: {
                root.reloadTick;
                return root.actionValue(root.actBorderVisible, "value", false);
            }
            onToggleClicked: checked => {
                return root.writeAction(root.actBorderVisible, {
                    "value": checked
                });
            }
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Border width")
                    searchAnchor: "borderWidth"
                    description: i18n("Thickness of the colored border around windows")

                    SettingsSpinBox {
                        from: root.bounds.borderWidthMin
                        to: root.bounds.borderWidthMax
                        value: {
                            root.reloadTick;
                            return root.actionValue(root.actBorderWidth, "value", root.bounds.borderWidthMin);
                        }
                        onValueModified: value => {
                            return root.writeAction(root.actBorderWidth, {
                                "value": value
                            });
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Corner radius")
                    searchAnchor: "cornerRadius"
                    description: i18n("Roundness of the border corners (0 for square)")

                    SettingsSpinBox {
                        from: root.bounds.borderRadiusMin
                        to: root.bounds.borderRadiusMax
                        value: {
                            root.reloadTick;
                            return root.actionValue(root.actBorderRadius, "value", root.bounds.borderRadiusMin);
                        }
                        onValueModified: value => {
                            return root.writeAction(root.actBorderRadius, {
                                "value": value
                            });
                        }
                    }
                }
            }
        }

        // =================================================================
        // Colors Card — active + inactive border colours, with a system
        // accent toggle that writes the "accent" sentinel.
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Colors")
            searchAnchor: "colors"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Use system accent color")
                    searchAnchor: "useSystemAccentColor"
                    description: i18n("Follow the system color scheme for the border color")

                    SettingsSwitch {
                        id: useAccentSwitch

                        // True when the active colour carries the accent sentinel.
                        checked: {
                            root.reloadTick;
                            return root.actionValue(root.actBorderColor, "active", root.defaultBorderHex) === root.accentToken;
                        }
                        accessibleName: i18n("Use system accent color")
                        onToggled: function (newValue) {
                            if (newValue) {
                                root.writeAction(root.actBorderColor, {
                                    "active": root.accentToken,
                                    "inactive": root.accentToken
                                });
                            } else {
                                root.writeAction(root.actBorderColor, {
                                    "active": root.defaultBorderHex,
                                    "inactive": root.defaultBorderHex
                                });
                            }
                        }
                    }
                }

                SettingsSeparator {
                    visible: !useAccentSwitch.checked
                }

                SettingsRow {
                    visible: !useAccentSwitch.checked
                    title: i18n("Active border color")
                    searchAnchor: "activeBorderColor"
                    description: i18n("Border color for the focused window")

                    ColorSwatchRow {
                        color: {
                            root.reloadTick;
                            return root.actionValue(root.actBorderColor, "active", root.defaultBorderHex);
                        }
                        onClicked: {
                            activeBorderColorDialog.selectedColor = root.actionValue(root.actBorderColor, "active", root.defaultBorderHex);
                            activeBorderColorDialog.open();
                        }
                    }
                }

                SettingsSeparator {
                    visible: !useAccentSwitch.checked
                }

                SettingsRow {
                    visible: !useAccentSwitch.checked
                    title: i18n("Inactive border color")
                    searchAnchor: "inactiveBorderColor"
                    description: i18n("Border color for unfocused windows")

                    ColorSwatchRow {
                        color: {
                            root.reloadTick;
                            return root.actionValue(root.actBorderColor, "inactive", root.defaultBorderHex);
                        }
                        onClicked: {
                            inactiveBorderColorDialog.selectedColor = root.actionValue(root.actBorderColor, "inactive", root.defaultBorderHex);
                            inactiveBorderColorDialog.open();
                        }
                    }
                }
            }
        }

        // =================================================================
        // Decorations Card — hide title bars.
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Decorations")
            searchAnchor: "decorations"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Hide title bars")
                    searchAnchor: "hideTitleBars"
                    description: i18n("Remove window title bars, restored when a window floats")

                    SettingsSwitch {
                        checked: {
                            root.reloadTick;
                            return root.actionValue(root.actHideTitleBar, "value", false);
                        }
                        accessibleName: i18n("Hide title bars")
                        onToggled: function (newValue) {
                            root.writeAction(root.actHideTitleBar, {
                                "value": newValue
                            });
                        }
                    }
                }
            }
        }

        // =================================================================
        // Gaps Card — the unified inner/outer gap model, rule-backed on the
        // same baseline rule. Smart gaps is tiling-only and lives on the
        // Tiling → Window page, so it is hidden here.
        // =================================================================
        GapsSettingsCard {
            Layout.fillWidth: true
            searchAnchor: "gaps"
            gapMin: root.bounds.outerGapMin
            gapMax: root.bounds.outerGapMax
            primaryGapMin: root.bounds.innerGapMin
            primaryGapMax: root.bounds.innerGapMax
            primaryGapLabel: i18n("Inner gap")
            primaryGapDescription: i18n("Space between windows")
            outerGapLabel: i18n("Outer gap")
            outerGapDescription: i18n("Space from the screen edges to windows")
            showSmartGaps: false
            primaryGapValue: {
                root.reloadTick;
                return root.actionValue(root.actInnerGap, "value", root.bounds.innerGapMin);
            }
            outerGapValue: {
                root.reloadTick;
                return root.actionValue(root.actOuterGap, "value", root.bounds.outerGapMin);
            }
            usePerSideOuterGap: {
                root.reloadTick;
                return root.actionValue(root.actUsePerSideOuterGap, "value", false);
            }
            outerGapTopValue: {
                root.reloadTick;
                return root.actionValue(root.actOuterGapTop, "value", root.bounds.outerGapMin);
            }
            outerGapBottomValue: {
                root.reloadTick;
                return root.actionValue(root.actOuterGapBottom, "value", root.bounds.outerGapMin);
            }
            outerGapLeftValue: {
                root.reloadTick;
                return root.actionValue(root.actOuterGapLeft, "value", root.bounds.outerGapMin);
            }
            outerGapRightValue: {
                root.reloadTick;
                return root.actionValue(root.actOuterGapRight, "value", root.bounds.outerGapMin);
            }
            onPrimaryGapModified: value => {
                return root.writeAction(root.actInnerGap, {
                    "value": value
                });
            }
            onOuterGapModified: value => {
                return root.writeAction(root.actOuterGap, {
                    "value": value
                });
            }
            onUsePerSideOuterGapToggled: checked => {
                return root.writeAction(root.actUsePerSideOuterGap, {
                    "value": checked
                });
            }
            onOuterGapTopModified: value => {
                return root.writeAction(root.actOuterGapTop, {
                    "value": value
                });
            }
            onOuterGapBottomModified: value => {
                return root.writeAction(root.actOuterGapBottom, {
                    "value": value
                });
            }
            onOuterGapLeftModified: value => {
                return root.writeAction(root.actOuterGapLeft, {
                    "value": value
                });
            }
            onOuterGapRightModified: value => {
                return root.writeAction(root.actOuterGapRight, {
                    "value": value
                });
            }
        }

        // =================================================================
        // Per-window overrides — link to the Window Rules page.
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Per-window appearance")
            searchAnchor: "perWindow"
            collapsible: false

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("These settings apply to every window. To override the border or title bar for a specific app or monitor, add a window rule.")
                }

                Button {
                    text: i18n("Open Window Rules")
                    icon.name: "view-list-details"
                    onClicked: settingsController.activePage = "window-rules"
                }
            }
        }
    }

    // =====================================================================
    // Color Dialogs
    // =====================================================================
    ColorDialog {
        id: activeBorderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Active Border Color")
        onAccepted: root.writeAction(root.actBorderColor, {
            "active": root.colorToHex(selectedColor)
        })
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Inactive Border Color")
        onAccepted: root.writeAction(root.actBorderColor, {
            "inactive": root.colorToHex(selectedColor)
        })
    }
}
