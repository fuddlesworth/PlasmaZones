// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations → General — global defaults that apply to every event.
 *
 * This is the "global" branch of the inheritance tree: any sub-page event
 * without its own override falls back through its category up to here.
 * Edits route through `appSettings.animation*` (the existing config-driven
 * `Global` profile in `kSettingsDrivenProfilePaths`), not through the
 * per-event ProfileLoader pipeline — so this page intentionally bypasses
 * `AnimationsPageController.setOverride` and writes through the same
 * `Settings::animationProfile` Q_PROPERTYs the legacy General > Animations
 * card has always used.
 *
 * Timing-mode toggle mirrors PR #291's per-event card: writing a
 * `"spring:omega,zeta"` string into `animationEasingCurve` flips the
 * Global profile to spring physics (the curve string itself is the
 * discriminator — `Settings::animationProfile` parses both forms via
 * `CurveRegistry`). The `_lastEasingCurve` / `_lastSpringOmega/Zeta`
 * state preserves each axis across mode toggles so previewing Spring
 * doesn't permanently lose the user's easing curve.
 */
SettingsFlickable {
    id: page

    readonly property QtObject appSettings: settingsController.settings
    // The min-size window filters are rule-backed: each is the threshold in the
    // match of a managed animation min-size baseline ExcludeAnimations rule
    // (Width / Height LessThan N). The baseline is always daemon-seeded (0 =
    // never matches = off, the default), so the controls only ever UPDATE its
    // match threshold; read/write route through the RuleController. The
    // transient / notification toggles stay config-backed.
    readonly property var ruleController: settingsController.rulesPage
    property int animReloadTick: 0

    // Threshold N of a min-size baseline (Width/Height LessThan N), or 0 when
    // the rule is absent (fresh profile before the daemon seeds — 0 matches the
    // seeded off-by-default threshold, so nothing jumps once it lands). Reading
    // animReloadTick re-reads the value bindings after a write / rule reload.
    function minSizeThreshold(ruleId) {
        page.animReloadTick;
        const rule = page.ruleController.ruleJson(ruleId);
        if (rule && rule.id && rule.match && rule.match.value !== undefined)
            return rule.match.value;
        return 0;
    }

    // Write a min-size threshold onto the baseline rule's match (0 = disabled).
    // Mirrors GeneralPage.writeMinSize: no find-or-create — the daemon seeds the
    // baseline, so a missing rule is the transient pre-seed state and the write
    // is dropped (the spinbox snaps back to the current threshold).
    function writeMinSize(ruleId, field, value) {
        var rule = page.ruleController.ruleJson(ruleId);
        if (!rule || !rule.id)
            return;
        rule.match = {
            "field": field,
            "op": "lessThan",
            "value": Math.max(0, value)
        };
        page.ruleController.updateRuleFromJson(rule);
        page.animReloadTick++;
    }

    Connections {
        target: page.ruleController
        function onRulesLoaded() {
            page.animReloadTick++;
        }
        function onBaselinesChanged() {
            page.animReloadTick++;
        }
    }
    // ── Timing-mode state ───────────────────────────────────────────────
    // Computed from the live `animationEasingCurve` string: the "spring:"
    // prefix is the on-disk discriminator. Defining this as a binding
    // means an external write (e.g. from the Presets page's
    // applyAsDefault) flips the toggle without imperative refresh.
    readonly property string _springPrefix: "spring:"
    readonly property bool _isSpring: page._isSpringCurve(page.appSettings.animationEasingCurve)
    readonly property int _currentTimingMode: _isSpring ? CurvePresets.timingModeSpring : CurvePresets.timingModeEasing
    // Last-seen values per axis. When the user toggles the timing-mode
    // combo we write a fresh curve string from these, preserving the
    // other axis's value across the round trip. Initialized below from
    // the saved curve and kept in sync via Connections.
    property string _lastEasingCurve: CurvePresets.defaultEasingCurve
    property real _lastSpringOmega: CurvePresets.defaultSpringOmega
    property real _lastSpringZeta: CurvePresets.defaultSpringZeta

    function _isSpringCurve(curveStr) {
        return typeof curveStr === "string" && curveStr.indexOf(page._springPrefix) === 0;
    }

    function _parseSpring(curveStr) {
        var parts = curveStr.substring(page._springPrefix.length).split(",");
        var w = parseFloat(parts[0]);
        var z = parseFloat(parts[1]);
        return {
            "omega": isFinite(w) ? w : CurvePresets.defaultSpringOmega,
            "zeta": isFinite(z) ? z : CurvePresets.defaultSpringZeta
        };
    }

    // Refresh the cached easing/spring values from whichever axis the
    // current curve string describes. Called once on load and whenever
    // `animationEasingCurve` changes externally.
    function _syncCachedValues() {
        var c = page.appSettings.animationEasingCurve;
        if (typeof c !== "string")
            return;

        if (page._isSpringCurve(c)) {
            var s = page._parseSpring(c);
            page._lastSpringOmega = s.omega;
            page._lastSpringZeta = s.zeta;
        } else if (c.length > 0) {
            page._lastEasingCurve = c;
        }
    }

    function _writeSpring(omega, zeta) {
        var omegaRounded = parseFloat(omega.toFixed(2));
        var zetaRounded = parseFloat(zeta.toFixed(2));
        var encoded = page._springPrefix + omegaRounded + "," + zetaRounded;
        // No-op-guard before mutating cache so a re-selection of the
        // already-active mode/values doesn't churn bindings even though
        // the underlying setter would no-op.
        if (page.appSettings.animationEasingCurve === encoded)
            return;

        // Cache the ROUNDED values — the encoded string is the canonical
        // on-disk form (2-decimal precision), so caching the raw inputs
        // would briefly leave `_lastSpringOmega` / `_lastSpringZeta` a
        // sub-precision tick off the value the next reload sees, until
        // `_syncCachedValues()` re-parses the encoded string.
        page._lastSpringOmega = omegaRounded;
        page._lastSpringZeta = zetaRounded;
        page.appSettings.animationEasingCurve = encoded;
    }

    function _writeEasing(curveStr) {
        if (page.appSettings.animationEasingCurve === curveStr)
            return;

        page._lastEasingCurve = curveStr;
        page.appSettings.animationEasingCurve = curveStr;
    }

    // ── Shared curve/timing editor ↔ Settings-driven Global profile ─────
    // The card now drives curve + duration through the same
    // AnimationProfileEditor the per-event override cards use, instead of an
    // inline bezier canvas. The editor owns its working state; we seed it from
    // the saved profile and commit its changes back. Set while WE are writing
    // so the change-signal handlers below don't re-seed the editor mid-edit.
    property bool _committingEditor: false

    // Push the saved profile into the editor. Property assignment doesn't emit
    // the editor's valueChanged, so this never re-enters _commitEditor().
    function _seedEditor() {
        editor.timingMode = page._currentTimingMode;
        editor.easingCurve = page._lastEasingCurve;
        editor.springOmega = page._lastSpringOmega;
        editor.springZeta = page._lastSpringZeta;
        editor.duration = page.appSettings.animationDuration;
    }

    // Commit the editor's working state back to the Global profile.
    function _commitEditor() {
        page._committingEditor = true;
        if (editor.timingMode === CurvePresets.timingModeSpring)
            page._writeSpring(editor.springOmega, editor.springZeta);
        else
            page._writeEasing(editor.easingCurve);
        if (page.appSettings.animationDuration !== editor.duration)
            page.appSettings.animationDuration = editor.duration;
        page._committingEditor = false;
    }

    contentHeight: content.implicitHeight
    clip: true
    Component.onCompleted: {
        page._syncCachedValues();
        page._seedEditor();
    }

    Connections {
        function onAnimationEasingCurveChanged() {
            page._syncCachedValues();
            // Don't re-seed while WE'RE the writer — the editor already holds
            // the committed value, and re-seeding mid-edit (e.g. during a
            // duration drag) would fight the live control.
            if (!page._committingEditor)
                page._seedEditor();
        }
        function onAnimationDurationChanged() {
            if (!page._committingEditor)
                page._seedEditor();
        }

        target: page.appSettings
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("These defaults apply to every animation event unless a sub-page (Window, Zone, OSD, etc.) defines its own override.")
        }

        SettingsCard {
            // ── Mode-agnostic behaviour ───────────────────────────────
            // These rows used to live inside EasingSettings; pulled
            // out so they remain visible (and editable) in spring
            // mode — they configure how the daemon sequences and
            // gates animations regardless of the timing curve.

            id: animationsCard

            Layout.fillWidth: true
            headerText: i18n("Global animation defaults")
            searchAnchor: "globalAnimationDefaults"
            showToggle: true
            toggleChecked: page.appSettings.animationsEnabled
            collapsible: true
            onToggleClicked: function (checked) {
                page.appSettings.animationsEnabled = checked;
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // ── Curve / timing editor ─────────────────────────────────
                // Compact curve summary (thumbnail + "Customize…" → dialog) plus
                // the timing-mode and duration rows — the same
                // AnimationProfileEditor the per-event override cards use. This
                // replaces the large inline bezier canvas, which read as a
                // confusing "advanced editor" sitting on the defaults page. The
                // editor is seeded from / committed to the Settings-driven Global
                // profile (animationEasingCurve / animationDuration) via
                // _seedEditor() / _commitEditor().
                AnimationProfileEditor {
                    id: editor

                    Layout.fillWidth: true
                    enabled: animationsCard.toggleChecked
                    // The Global default has no shader leg in this UI — shader
                    // overrides live on the per-event and Rules layers.
                    shaderLegSupported: false
                    showShaderSection: false
                    eventLabel: i18n("Global animation defaults")
                    onValueChanged: page._commitEditor()
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Multiple windows")
                    searchAnchor: "multipleWindows"
                    description: i18n("How to animate when moving several windows at once")

                    WideComboBox {
                        Accessible.name: i18n("Multiple windows")
                        enabled: animationsCard.toggleChecked
                        model: [i18n("All at once"), i18n("One by one")]
                        currentIndex: page.appSettings.animationSequenceMode
                        onActivated: index => {
                            page.appSettings.animationSequenceMode = index;
                        }
                    }
                }

                SettingsRow {
                    visible: page.appSettings.animationSequenceMode === 1
                    title: i18n("Stagger delay")
                    searchAnchor: "staggerDelay"
                    description: i18n("Pause between each window's animation start")

                    SettingsSlider {
                        Accessible.name: i18n("Stagger delay")
                        enabled: animationsCard.toggleChecked
                        from: settingsController.generalPage.animationStaggerIntervalMin
                        to: settingsController.generalPage.animationStaggerIntervalMax
                        stepSize: 10
                        value: page.appSettings.animationStaggerInterval
                        valueSuffix: " ms"
                        labelWidth: Kirigami.Units.gridUnit * 4
                        onMoved: value => {
                            page.appSettings.animationStaggerInterval = Math.round(value);
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Minimum distance")
                    searchAnchor: "minimumDistance"
                    description: page.appSettings.animationMinDistance === 0 ? i18n("Always animates. No distance threshold is set.") : i18n("Skip animation when geometry changes less than this")

                    SettingsSpinBox {
                        Accessible.name: i18n("Minimum distance")
                        enabled: animationsCard.toggleChecked
                        from: settingsController.generalPage.animationMinDistanceMin
                        to: settingsController.generalPage.animationMinDistanceMax
                        stepSize: 5
                        value: page.appSettings.animationMinDistance
                        // Match the "zero = disabled, otherwise px" treatment used
                        // by the Minimum window width / height spinboxes below so
                        // the user sees a consistent "Off" / "%1 px" rendering
                        // across every threshold-with-disable spinbox on the page.
                        unitText: ""
                        onValueModified: value => {
                            page.appSettings.animationMinDistance = value;
                        }
                        textFromValue: function (value) {
                            return value === 0 ? i18n("Off") : i18nc("pixel-unit suffix in spin box", "%1 px", value);
                        }
                    }
                }
            }
        }

        // ── Window Filtering ─────────────────────────────────────────
        // Animation-global gating: which windows are eligible for any
        // animation at all. Relocated here from the old Animations App
        // Rules page — these are global config toggles, not per-window
        // rules, so they belong on the General page. Per-window
        // overrides now live on the unified Rules page.
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Filtered windows are not animated. Use a Rule to keep a specific application animated even when a filter would exclude it.")
        }

        SettingsCard {
            id: filteringCard

            Layout.fillWidth: true
            headerText: i18n("Window Filtering")
            searchAnchor: "windowFiltering"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Exclude transient windows")
                    searchAnchor: "excludeTransientWindows"
                    description: i18n("Skip animations for dialogs, popups, tooltips, and dropdown menus")

                    SettingsSwitch {
                        checked: page.appSettings.animationExcludeTransientWindows
                        accessibleName: i18n("Exclude transient windows from animations")
                        onToggled: function (newValue) {
                            page.appSettings.animationExcludeTransientWindows = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Exclude notifications and OSDs")
                    searchAnchor: "excludeNotificationsAndOsds"
                    description: i18n("Skip animations for notification popups and on-screen displays such as volume and brightness")

                    SettingsSwitch {
                        checked: page.appSettings.animationExcludeNotificationsAndOsd
                        accessibleName: i18n("Exclude notifications and on-screen displays from animations")
                        onToggled: function (newValue) {
                            page.appSettings.animationExcludeNotificationsAndOsd = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Minimum window width")
                    searchAnchor: "minimumWindowWidth"
                    description: page.minSizeThreshold(settingsController.animationMinWidthRuleId()) === 0 ? i18n("Disabled. No width threshold.") : i18n("Windows narrower than this will not animate")

                    SettingsSpinBox {
                        // Schema-driven bounds — see GeneralPageController's
                        // animationMinimumWindowWidthMin/Max Q_PROPERTYs.
                        // Literal bounds would silently truncate any saved
                        // value outside the literal range when the SpinBox
                        // clamped the bound `value` on render.
                        from: settingsController.generalPage.animationMinimumWindowWidthMin
                        to: settingsController.generalPage.animationMinimumWindowWidthMax
                        stepSize: 10
                        value: page.minSizeThreshold(settingsController.animationMinWidthRuleId())
                        // textFromValue already emits the localised "%1 px" suffix; suppress
                        // SettingsSpinBox's default "px" Label so the displayed value reads
                        // "100 px" rather than "100 px px" (and "Off" rather than "Off px").
                        unitText: ""
                        Accessible.name: i18n("Minimum window width for animations")
                        onValueModified: value => {
                            // Not translated — Rule::name is the persisted identity
                            // surface and must match the v4→v5 migration's spelling.
                            page.writeMinSize(settingsController.animationMinWidthRuleId(), "width", value);
                        }
                        textFromValue: function (value) {
                            return value === 0 ? i18n("Off") : i18nc("pixel-unit suffix in spin box", "%1 px", value);
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Minimum window height")
                    searchAnchor: "minimumWindowHeight"
                    description: page.minSizeThreshold(settingsController.animationMinHeightRuleId()) === 0 ? i18n("Disabled. No height threshold.") : i18n("Windows shorter than this will not animate")

                    SettingsSpinBox {
                        from: settingsController.generalPage.animationMinimumWindowHeightMin
                        to: settingsController.generalPage.animationMinimumWindowHeightMax
                        stepSize: 10
                        value: page.minSizeThreshold(settingsController.animationMinHeightRuleId())
                        // textFromValue already emits the localised "%1 px" suffix; see the
                        // width SettingsSpinBox above for rationale on suppressing
                        // SettingsSpinBox's default "px" Label.
                        unitText: ""
                        Accessible.name: i18n("Minimum window height for animations")
                        onValueModified: value => {
                            page.writeMinSize(settingsController.animationMinHeightRuleId(), "height", value);
                        }
                        textFromValue: function (value) {
                            return value === 0 ? i18n("Off") : i18nc("pixel-unit suffix in spin box", "%1 px", value);
                        }
                    }
                }
            }
        }
    }
}
