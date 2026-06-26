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

    // Gap scope follows the shared monitor scope chip (settingsController.
    // scopeScreenName): "" = global (edits the baseline rule's gap actions);
    // otherwise the monitor whose gap override rule is being edited. The
    // border/title-bar cards always edit the global baseline; only the Gaps card
    // is scope-aware.
    readonly property string gapScope: settingsController.scopeScreenName

    contentHeight: content.implicitHeight
    clip: true

    // Read one action param from the rule @p ruleId, or @p fallback if absent.
    function actionValueFrom(ruleId, typeWire, paramKey, fallback) {
        if (!ruleId) {
            return fallback;
        }
        const rule = ruleController.ruleJson(ruleId);
        const actions = rule.actions || [];
        for (var i = 0; i < actions.length; ++i) {
            if (actions[i].type === typeWire) {
                const v = actions[i][paramKey];
                return v === undefined ? fallback : v;
            }
        }
        return fallback;
    }

    // Read one action param from the baseline rule, or @p fallback if absent.
    function actionValue(typeWire, paramKey, fallback) {
        return root.actionValueFrom(root.baselineId, typeWire, paramKey, fallback);
    }

    // ─── Per-monitor gap overrides (screen-scoped rules) ─────────────────────

    // The deterministic id of the current monitor's gap rule, or "" when global.
    function perScreenGapId() {
        return root.gapScope === "" ? "" : root.bounds.perScreenGapRuleId(root.gapScope);
    }

    // True when the current monitor scope already carries a gap override rule.
    function perScreenGapRuleExists() {
        const id = root.perScreenGapId();
        if (id === "") {
            return false;
        }
        const rule = ruleController.ruleJson(id);
        return rule && rule.id ? true : false;
    }

    // The rule the gap controls READ from: the per-screen rule when one exists,
    // otherwise the baseline rule (so a not-yet-overridden monitor shows the
    // inherited global values as its starting point).
    function gapReadId() {
        if (root.gapScope === "") {
            return root.baselineId;
        }
        return root.perScreenGapRuleExists() ? root.perScreenGapId() : root.baselineId;
    }

    // Read a gap action param honouring the current scope.
    function gapValue(typeWire, paramKey, fallback) {
        return root.actionValueFrom(root.gapReadId(), typeWire, paramKey, fallback);
    }

    // Friendly name for a screen connector id (falls back to the raw id).
    function scopeDisplayName(connector) {
        const arr = settingsController.screens || [];
        for (var i = 0; i < arr.length; ++i) {
            if ((arr[i].name || "") === connector) {
                return arr[i].displayLabel || arr[i].name || connector;
            }
        }
        return connector;
    }

    // Merge @p params into the action of @p typeWire on @p ruleObj (creating the
    // action if absent). Mutates and returns the rule object.
    function applyGapParam(ruleObj, typeWire, params) {
        const actions = (ruleObj.actions || []).slice();
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
        ruleObj.actions = actions;
        return ruleObj;
    }

    // Build a fresh per-screen gap rule for the current scope, seeding every gap
    // action from the global baseline's current values so the override starts as
    // an exact copy of what the monitor already inherited.
    function buildPerScreenGapRule(id) {
        const scope = root.gapScope;
        const baseVal = function (typeWire, fallback) {
            return root.actionValueFrom(root.baselineId, typeWire, "value", fallback);
        };
        return {
            "id": id,
            "name": i18n("Gaps (%1)", root.scopeDisplayName(scope)),
            "enabled": true,
            "match": {
                "field": "screenId",
                "op": "equals",
                "value": scope
            },
            "actions": [
                {
                    "type": root.actInnerGap,
                    "value": baseVal(root.actInnerGap, root.bounds.innerGapMin)
                },
                {
                    "type": root.actOuterGap,
                    "value": baseVal(root.actOuterGap, root.bounds.outerGapMin)
                },
                {
                    "type": root.actUsePerSideOuterGap,
                    "value": baseVal(root.actUsePerSideOuterGap, false)
                },
                {
                    "type": root.actOuterGapTop,
                    "value": baseVal(root.actOuterGapTop, root.bounds.outerGapMin)
                },
                {
                    "type": root.actOuterGapBottom,
                    "value": baseVal(root.actOuterGapBottom, root.bounds.outerGapMin)
                },
                {
                    "type": root.actOuterGapLeft,
                    "value": baseVal(root.actOuterGapLeft, root.bounds.outerGapMin)
                },
                {
                    "type": root.actOuterGapRight,
                    "value": baseVal(root.actOuterGapRight, root.bounds.outerGapMin)
                }
            ]
        };
    }

    // Write a gap action honouring the current scope. Global scope rewrites the
    // baseline rule; a monitor scope finds-or-creates that monitor's gap rule.
    function writeGapAction(typeWire, params) {
        if (root.gapScope === "") {
            root.writeAction(typeWire, params);
            return;
        }
        const id = root.perScreenGapId();
        var rule = ruleController.ruleJson(id);
        if (!rule || !rule.id) {
            rule = root.buildPerScreenGapRule(id);
            root.applyGapParam(rule, typeWire, params);
            ruleController.addRuleFromJson(rule);
        } else {
            root.applyGapParam(rule, typeWire, params);
            ruleController.updateRuleFromJson(rule);
        }
        root.reloadTick++;
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

    // Re-evaluate the gap value bindings when the monitor scope chip changes the
    // active scope, or when a per-screen gap rule is added / cleared (the chip's
    // clear and rule-model changes both surface as perScreenOverridesChanged).
    Connections {
        target: settingsController
        function onScopeScreenNameChanged() {
            root.reloadTick++;
        }
        function onPerScreenOverridesChanged() {
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
            // Per-monitor scoping via the shared SettingsCard header chip: "All
            // monitors" edits the global baseline rule; picking a monitor edits
            // that monitor's gap override rule. has/clear go through the
            // controller's rule-backed per-screen gap methods.
            scopeEnabled: true
            scopeAppSettings: settingsController
            scopeHasOverridesMethod: "hasPerScreenGapRule"
            scopeClearerMethod: "clearPerScreenGapRule"
            primaryGapValue: {
                root.reloadTick;
                return root.gapValue(root.actInnerGap, "value", root.bounds.innerGapMin);
            }
            outerGapValue: {
                root.reloadTick;
                return root.gapValue(root.actOuterGap, "value", root.bounds.outerGapMin);
            }
            usePerSideOuterGap: {
                root.reloadTick;
                return root.gapValue(root.actUsePerSideOuterGap, "value", false);
            }
            outerGapTopValue: {
                root.reloadTick;
                return root.gapValue(root.actOuterGapTop, "value", root.bounds.outerGapMin);
            }
            outerGapBottomValue: {
                root.reloadTick;
                return root.gapValue(root.actOuterGapBottom, "value", root.bounds.outerGapMin);
            }
            outerGapLeftValue: {
                root.reloadTick;
                return root.gapValue(root.actOuterGapLeft, "value", root.bounds.outerGapMin);
            }
            outerGapRightValue: {
                root.reloadTick;
                return root.gapValue(root.actOuterGapRight, "value", root.bounds.outerGapMin);
            }
            onPrimaryGapModified: value => {
                return root.writeGapAction(root.actInnerGap, {
                    "value": value
                });
            }
            onOuterGapModified: value => {
                return root.writeGapAction(root.actOuterGap, {
                    "value": value
                });
            }
            onUsePerSideOuterGapToggled: checked => {
                return root.writeGapAction(root.actUsePerSideOuterGap, {
                    "value": checked
                });
            }
            onOuterGapTopModified: value => {
                return root.writeGapAction(root.actOuterGapTop, {
                    "value": value
                });
            }
            onOuterGapBottomModified: value => {
                return root.writeGapAction(root.actOuterGapBottom, {
                    "value": value
                });
            }
            onOuterGapLeftModified: value => {
                return root.writeGapAction(root.actOuterGapLeft, {
                    "value": value
                });
            }
            onOuterGapRightModified: value => {
                return root.writeGapAction(root.actOuterGapRight, {
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
