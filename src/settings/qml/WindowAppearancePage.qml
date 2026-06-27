// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Slim "Window Appearance" page. The window border, title-bar, and gap defaults
// are resolved from three managed baseline appearance Rules (the
// catch-all, lowest-priority rules the daemon seeds, one per concern). This page
// is a friendly editor for those three rules: each action's read/write routes to
// the matching baseline rule through the Rules controller. Per-window
// overrides are ordinary higher-priority rules edited on the Rules page
// (the link at the bottom).
SettingsFlickable {
    id: root

    // Bounds + the three baseline rule ids.
    readonly property var bounds: settingsController.windowAppearancePage
    // The Rules controller owns the rule model + the daemon round-trip.
    readonly property var ruleController: settingsController.rulesPage
    // One managed baseline rule per concern.
    readonly property string borderId: bounds.borderBaselineRuleId
    readonly property string titleBarId: bounds.titleBarBaselineRuleId
    readonly property string gapId: bounds.gapBaselineRuleId

    // Action wire strings (mirrors PhosphorRules::ActionType).
    readonly property string actBorderVisible: "setBorderVisible"
    readonly property string actBorderWidth: "setBorderWidth"
    readonly property string actBorderRadius: "setBorderRadius"
    readonly property string actBorderColorActive: "setBorderColorActive"
    readonly property string actBorderColorInactive: "setBorderColorInactive"
    readonly property string actHideTitleBar: "setHideTitleBar"
    // Gap action wire strings (mirrors PhosphorRules::ActionType). The
    // shared inner/outer gap model is rule-backed too, so the Gaps card edits the
    // same baseline rule as the border controls.
    readonly property string actInnerGap: "setInnerGap"
    readonly property string actOuterGap: "setOuterGap"
    readonly property string actUsePerSideOuterGap: "setUsePerSideOuterGap"
    readonly property string actOuterGapTop: "setOuterGapTop"
    readonly property string actOuterGapBottom: "setOuterGapBottom"
    readonly property string actOuterGapLeft: "setOuterGapLeft"
    readonly property string actOuterGapRight: "setOuterGapRight"
    // "Follow the system accent" sentinel (PhosphorRules::BorderColorToken::Accent).
    // The effect resolves it to the live system accent colour at apply time, so a
    // border colour written as this token tracks Plasma accent changes without a
    // rule edit.
    readonly property string accentToken: "accent"
    // Concrete fallback colour written when the user turns the accent toggle off
    // (matches the daemon's seeded baseline default — KDE accent blue, opaque).
    readonly property string defaultBorderHex: "#FF3DAEE9"

    // Bumped whenever the rule changes (daemon reload or a local write) so the
    // function-call value bindings below re-evaluate against the fresh rule.
    property int reloadTick: 0

    // Gap scope follows the shared monitor scope chip (settingsController.
    // scopeScreenName): "" = global (edits the gap baseline rule's actions);
    // otherwise the monitor whose gap override rule is being edited. The
    // border/title-bar cards always edit their global baseline; only the Gaps
    // card is scope-aware.
    readonly property string gapScope: settingsController.scopeScreenName

    // True when the border is shown. The border detail controls (width, radius,
    // colours) are hidden while the border is off so the user cannot edit them
    // and thereby re-add the dependent actions the baseline deliberately omits.
    readonly property bool borderVisible: {
        root.reloadTick;
        return root.actionValue(root.actBorderVisible, "value", false) === true;
    }

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

    // Map an action wire type to the managed baseline rule that owns it: the gap
    // actions live on the gap baseline, hide-title-bar on the title-bar baseline,
    // and every border action on the border baseline.
    function baselineFor(typeWire) {
        switch (typeWire) {
        case root.actHideTitleBar:
            return root.titleBarId;
        case root.actInnerGap:
        case root.actOuterGap:
        case root.actUsePerSideOuterGap:
        case root.actOuterGapTop:
        case root.actOuterGapBottom:
        case root.actOuterGapLeft:
        case root.actOuterGapRight:
            return root.gapId;
        default:
            return root.borderId;
        }
    }

    // Read one action param from the baseline rule that owns @p typeWire, or
    // @p fallback if absent.
    function actionValue(typeWire, paramKey, fallback) {
        return root.actionValueFrom(root.baselineFor(typeWire), typeWire, paramKey, fallback);
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
            return root.gapId;
        }
        return root.perScreenGapRuleExists() ? root.perScreenGapId() : root.gapId;
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
            return root.actionValueFrom(root.gapId, typeWire, "value", fallback);
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

    // Write one or more param values onto the action of the given type on the
    // baseline rule that owns it (creating the action if it is somehow absent),
    // then push the updated rule back through the controller. @p params is a
    // plain object of { paramKey: value } pairs.
    function writeAction(typeWire, params) {
        const rule = ruleController.ruleJson(root.baselineFor(typeWire));
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

    // True when the baseline rule that owns @p typeWire already carries an
    // action of that type. Used to seed a dependent action only when it is
    // absent (so turning a feature back on does not clobber a value the user
    // previously set, while the baseline never lists an inert dependent action).
    function hasAction(typeWire) {
        const rule = ruleController.ruleJson(root.baselineFor(typeWire));
        const actions = (rule && rule.actions) || [];
        for (var i = 0; i < actions.length; ++i) {
            if (actions[i].type === typeWire) {
                return true;
            }
        }
        return false;
    }

    // Remove the action of @p typeWire from the baseline rule that owns it (a
    // no-op when absent), then push the trimmed rule back through the
    // controller. Used to drop a dependent action when its parent feature is
    // turned off so the baseline carries only the actions actually in force.
    function removeAction(typeWire) {
        const rule = ruleController.ruleJson(root.baselineFor(typeWire));
        if (!rule || !rule.id) {
            return;
        }
        const actions = (rule.actions || []).filter(function (a) {
            return a.type !== typeWire;
        });
        rule.actions = actions;
        ruleController.updateRuleFromJson(rule);
        root.reloadTick++;
    }

    // Remove a gap action honouring the current scope, mirroring writeGapAction:
    // global scope drops it from the gap baseline rule; a monitor scope drops it
    // from that monitor's gap override rule (a no-op when neither the rule nor
    // the action exists).
    function removeGapAction(typeWire) {
        if (root.gapScope === "") {
            root.removeAction(typeWire);
            return;
        }
        const id = root.perScreenGapId();
        var rule = ruleController.ruleJson(id);
        if (!rule || !rule.id) {
            return;
        }
        const actions = (rule.actions || []).filter(function (a) {
            return a.type !== typeWire;
        });
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
                // The baseline border rule carries only the "show border" parent
                // action. Turning the border on seeds the dependent details
                // (width, radius, active/inactive colour), seeding any that is
                // absent from the same defaults the daemon used. Turning it off
                // removes them again so the baseline stays minimal.
                if (checked) {
                    if (!root.hasAction(root.actBorderWidth)) {
                        root.writeAction(root.actBorderWidth, {
                            "value": root.bounds.borderWidthDefault
                        });
                    }
                    if (!root.hasAction(root.actBorderRadius)) {
                        root.writeAction(root.actBorderRadius, {
                            "value": root.bounds.borderRadiusDefault
                        });
                    }
                    if (!root.hasAction(root.actBorderColorActive)) {
                        root.writeAction(root.actBorderColorActive, {
                            "value": root.accentToken
                        });
                    }
                    if (!root.hasAction(root.actBorderColorInactive)) {
                        root.writeAction(root.actBorderColorInactive, {
                            "value": root.accentToken
                        });
                    }
                } else {
                    root.removeAction(root.actBorderWidth);
                    root.removeAction(root.actBorderRadius);
                    root.removeAction(root.actBorderColorActive);
                    root.removeAction(root.actBorderColorInactive);
                }
                root.writeAction(root.actBorderVisible, {
                    "value": checked
                });
            }
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    visible: root.borderVisible
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

                SettingsSeparator {
                    visible: root.borderVisible
                }

                SettingsRow {
                    visible: root.borderVisible
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
            visible: root.borderVisible
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

                        // True when the focused colour carries the accent sentinel.
                        checked: {
                            root.reloadTick;
                            return root.actionValue(root.actBorderColorActive, "value", root.defaultBorderHex) === root.accentToken;
                        }
                        accessibleName: i18n("Use system accent color")
                        onToggled: function (newValue) {
                            const colorValue = newValue ? root.accentToken : root.defaultBorderHex;
                            root.writeAction(root.actBorderColorActive, {
                                "value": colorValue
                            });
                            root.writeAction(root.actBorderColorInactive, {
                                "value": colorValue
                            });
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
                            return root.actionValue(root.actBorderColorActive, "value", root.defaultBorderHex);
                        }
                        onClicked: {
                            activeBorderColorDialog.selectedColor = root.actionValue(root.actBorderColorActive, "value", root.defaultBorderHex);
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
                            const raw = root.actionValue(root.actBorderColorInactive, "value", root.defaultBorderHex);
                            return raw === root.accentToken ? Kirigami.Theme.highlightColor : raw;
                        }
                        onClicked: {
                            const raw = root.actionValue(root.actBorderColorInactive, "value", root.defaultBorderHex);
                            inactiveBorderColorDialog.selectedColor = raw === root.accentToken ? root.defaultBorderHex : raw;
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
        // gap baseline rule. Smart gaps is tiling-only and lives on the
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
                // The gap rule carries only the uniform outer gap by default.
                // Turning per-side gaps on seeds the four per-side actions,
                // seeding any that is absent from the current uniform outer gap
                // so each side starts where the uniform gap left off. Turning it
                // off removes them again so an absent side falls back to uniform.
                if (checked) {
                    const seed = root.gapValue(root.actOuterGap, "value", root.bounds.outerGapMin);
                    const sides = [root.actOuterGapTop, root.actOuterGapBottom, root.actOuterGapLeft, root.actOuterGapRight];
                    for (var i = 0; i < sides.length; ++i) {
                        root.writeGapAction(sides[i], {
                            "value": root.gapValue(sides[i], "value", seed)
                        });
                    }
                } else {
                    root.removeGapAction(root.actOuterGapTop);
                    root.removeGapAction(root.actOuterGapBottom);
                    root.removeGapAction(root.actOuterGapLeft);
                    root.removeGapAction(root.actOuterGapRight);
                }
                root.writeGapAction(root.actUsePerSideOuterGap, {
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
        // Per-window overrides — link to the Rules page.
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Per-window appearance")
            searchAnchor: "perWindow"
            collapsible: false

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Override per app or monitor")
                    description: i18n("These settings apply to every window. Add a rule to override the border or title bar for a specific app or monitor.")
                    searchAnchor: "perRules"

                    Button {
                        text: i18n("Open Rules")
                        icon.name: "view-list-details"
                        onClicked: settingsController.activePage = "rules"
                    }
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
        onAccepted: root.writeAction(root.actBorderColorActive, {
            "value": root.colorToHex(selectedColor)
        })
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Inactive Border Color")
        onAccepted: root.writeAction(root.actBorderColorInactive, {
            "value": root.colorToHex(selectedColor)
        })
    }
}
