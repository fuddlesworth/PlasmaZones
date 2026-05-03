// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable card for per-event animation configuration.
 *
 * Each card edits one event in the `PhosphorAnimation::ProfilePaths`
 * taxonomy (e.g. `zone.snapIn`, `osd.show`). The override toggle
 * creates/clears one Profile JSON file under
 * `~/.local/share/plasmazones/profiles/`; the daemon's existing
 * `ProfileLoader` watches that dir and live-reloads the registry.
 *
 * Phase 3 scope: timing-mode (Easing/Spring), curve thumbnail with
 * "Customize…" dialog, duration slider, inheritance breadcrumb. The
 * Animation-style combo + shader-param editor + scale-start slider
 * land in Phase 6 alongside the shader-picker controller.
 *
 * Required properties:
 *   - eventPath:  full path string from `ProfilePaths::` (e.g. "zone.snapIn")
 *   - eventLabel: human-readable label
 *
 * Optional properties:
 *   - isParentNode: bool — flips the inheritance banner copy
 *   - alwaysEnabled: bool — hides the override toggle (the global root)
 *   - collapsible: bool — header click collapses the body
 */
Item {
    id: root

    required property string eventPath
    required property string eventLabel
    property bool isParentNode: false
    property bool alwaysEnabled: false
    property bool collapsible: false
    // ── Internal state — one source of truth per UX axis ────────────
    // The on-disk schema is `Profile::toJson()` — a single `curve`
    // string that's either an easing wire format ("x1,y1,x2,y2",
    // "elastic-out:amp,per", etc.) or a spring ("spring:omega,zeta").
    // The card unpacks it on read into the working state below, and
    // re-packs on write. Easing and spring values are remembered
    // independently across timing-mode toggles so the user doesn't
    // lose their easing curve when previewing spring physics.
    property bool overrideEnabled: false
    property int currentTimingMode: CurvePresets.timingModeEasing
    property int currentDuration: CurvePresets.defaultDurationMs
    property string currentEasingCurve: CurvePresets.defaultEasingCurve
    property real currentSpringOmega: CurvePresets.defaultSpringOmega
    property real currentSpringZeta: CurvePresets.defaultSpringZeta
    // ── Shader assignment (independent of timing override) ──────────
    // Per-event shader override is independent of the motion override
    // toggle: a user can pick a shader for an event without overriding
    // its timing. Persistence routes through Settings::shaderProfile-
    // Tree (NOT the Profile-loader pipeline), so signal handling for
    // refreshes is separate.
    property string currentShaderEffectId: ""
    // Initialized from Q_INVOKABLE resolvedShaderProfile() — refresh on
    // shaderProfileChanged via the Connections block at the bottom.
    property var currentShaderParams: ({
    })
    readonly property string currentCurveString: {
        if (currentTimingMode === CurvePresets.timingModeSpring)
            return "spring:" + currentSpringOmega.toFixed(2) + "," + currentSpringZeta.toFixed(2);

        return currentEasingCurve;
    }
    // Cached resolved-profile lookup. The C++ Q_INVOKABLE walks the
    // parent chain on every call; before this cache, the inheritance-
    // banner Label re-evaluated `inheritSummaryText()` on every
    // `currentTimingMode` / `currentDuration` / `currentEasingCurve`
    // change (each binding dependency), so a single keystroke on the
    // duration slider drove N round-trips into C++ where N is the
    // number of cards on the page. The revision tick (_inheritRev)
    // invalidates only when the profile chain actually changes — see
    // the Connections block below that bumps it on overrideChanged.
    property int _inheritRev: 0
    readonly property var _inheritResolved: {
        _inheritRev;
        return settingsController.animationsPage.resolvedProfile(root.eventPath);
    }
    // True only for event paths the daemon's overlay service actually
    // consumes as a shader-leg surface. Gates the shader picker, the
    // inline param editor, and the inheritance "Shader: X" banner so
    // a user can't pick a shader on an unsupported path (e.g. the
    // "All Panel Events" parent or `panel.slideIn`) and silently
    // persist a dead override that the daemon resolver would shadow
    // any user-intended setting with via deeper-leaf-wins overlay.
    // Source-of-truth list: `src/core/animationshadersupportedpaths.h`.
    readonly property bool _shaderLegSupported: settingsController.animationsPage.supportsShaderLeg(root.eventPath)
    // Number of shader overrides on paths strictly DEEPER than this card's
    // eventPath. Only meaningful for parent-node cards: a stale leaf
    // override (e.g. `panel.popup.layoutPicker.show = "dissolve"` set in
    // a previous session) silently wins the deeper-leaf-overlay merge in
    // `ShaderProfileTree::resolve` and shadows the parent's value at
    // runtime. Surfaced via the warning banner below with a one-click
    // "Clear shadowing children" button. Refreshed on any
    // shaderProfileChanged signal (see Connections at line 222+).
    property int _shadowingChildrenCount: 0

    // ── Inheritance summary (italic "Current: …" line when override off) ─
    function inheritSummaryText() {
        var r = root._inheritResolved;
        var curve = r.curve || CurvePresets.defaultEasingCurve;
        var dur = r.duration !== undefined ? r.duration : CurvePresets.defaultDurationMs;
        if (typeof curve === "string" && curve.indexOf("spring:") === 0) {
            var parts = curve.substring(7).split(",");
            var w = parseFloat(parts[0]);
            var z = parseFloat(parts[1]);
            if (isFinite(w) && isFinite(z))
                return i18n("Spring · ω=%1 · ζ=%2", w.toFixed(1), z.toFixed(2));

            return i18n("Spring · Custom");
        }
        return i18n("%1 · %2 ms", CurvePresets.curveDisplayName(curve), Math.round(dur));
    }

    function parentChainText() {
        var chain = settingsController.animationsPage.parentChain(root.eventPath);
        // Drop chain[0] (self) — show only ancestors as "zone ← global"
        if (chain.length <= 1)
            return "";

        return chain.slice(1).join(" ← ");
    }

    function summaryDescription() {
        if (root.currentTimingMode === CurvePresets.timingModeSpring) {
            var si = CurvePresets.springPresetIndex(root.currentSpringOmega, root.currentSpringZeta);
            if (si >= 0)
                return i18n("Spring · %1", CurvePresets.springPresets[si].label);

            return i18n("Spring · Custom");
        }
        var idx = CurvePresets.findIndices(root.currentEasingCurve);
        if (idx.styleIndex >= 0)
            return CurvePresets.easingStyles[idx.styleIndex].label + " · " + CurvePresets.easingDirections[idx.dirIndex].label;

        return i18n("Easing · Custom");
    }

    function summarySecondary() {
        if (root.currentTimingMode === CurvePresets.timingModeSpring)
            return i18n("ω=%1 · ζ=%2", root.currentSpringOmega.toFixed(1), root.currentSpringZeta.toFixed(2));

        return i18n("%1 ms", root.currentDuration);
    }

    function refreshShaderFromTree() {
        var resolved = settingsController.animationsPage.resolvedShaderProfile(root.eventPath);
        root.currentShaderEffectId = (resolved && resolved.effectId) ? resolved.effectId : "";
        root.currentShaderParams = (resolved && resolved.parameters) ? resolved.parameters : ({
        });
        // Recompute deeper-override count on every shader-tree update —
        // the warning banner below depends on it. Cheap (O(N) over
        // overriddenPaths). Only meaningful for parent-node cards but
        // we always refresh so the binding stays consistent.
        root._shadowingChildrenCount = settingsController.animationsPage.shaderOverrideDescendantCount(root.eventPath);
    }

    function refreshFromTree() {
        var raw = settingsController.animationsPage.rawProfile(root.eventPath);
        var resolved = settingsController.animationsPage.resolvedProfile(root.eventPath);
        var hasRaw = raw && Object.keys(raw).length > 0;
        root.overrideEnabled = hasRaw;
        // Effective values feed the controls. When override is off the
        // controls preview "what would happen if you turned it on" =
        // the resolved profile from the parent chain. When on, the
        // raw fields decide.
        var effective = hasRaw ? raw : resolved;
        var curve = effective.curve;
        if (typeof curve === "string" && curve.indexOf("spring:") === 0) {
            var parts = curve.substring(7).split(",");
            var w = parseFloat(parts[0]);
            var z = parseFloat(parts[1]);
            if (isFinite(w) && isFinite(z)) {
                root.currentTimingMode = CurvePresets.timingModeSpring;
                root.currentSpringOmega = w;
                root.currentSpringZeta = z;
            }
        } else {
            root.currentTimingMode = CurvePresets.timingModeEasing;
            if (typeof curve === "string" && curve.length > 0)
                root.currentEasingCurve = curve;

        }
        root.currentDuration = effective.duration !== undefined ? effective.duration : CurvePresets.defaultDurationMs;
    }

    // Build the on-disk profile object from the working state and
    // commit it through the controller. The controller stamps the
    // `name` field automatically.
    function commitOverride() {
        var profile = {
            "curve": root.currentCurveString,
            "duration": root.currentDuration
        };
        settingsController.animationsPage.setOverride(root.eventPath, profile);
    }

    implicitHeight: card.implicitHeight
    Layout.fillWidth: true
    Component.onCompleted: {
        refreshFromTree();
        refreshShaderFromTree();
    }

    // Pick up changes from any path in the tree — could be this event
    // (user toggled override) or an ancestor (we're inheriting from it
    // and the inherited value just changed). The signal is per-path
    // but it's cheap to just refresh.
    Connections {
        function onOverrideChanged(path) {
            root.refreshFromTree();
            // The signal is per-path but the resolved profile depends on
            // the entire ancestor chain, so any change anywhere can shift
            // the inheritance banner. Bump the revision tick to invalidate
            // _inheritResolved.
            root._inheritRev++;
        }

        function onShaderProfileChanged(path) {
            root.refreshShaderFromTree();
        }

        target: settingsController.animationsPage
    }

    SettingsCard {
        // ── Shader effect picker (independent of timing override) ─
        // Independent of timing override — users can drop a shader on
        // an event without touching its timing. The visibility gate
        // `root._shaderLegSupported` is declared on the card root so
        // it's reachable from every nested binding below; declaring
        // it here would scope it to this ColumnLayout and the outer
        // `root.<id>` references would silently resolve to undefined
        // (defaulting `visible:` to true and showing the picker on
        // every event regardless of daemon support).

        id: card

        anchors.fill: parent
        headerText: root.eventLabel
        showToggle: !root.alwaysEnabled
        toggleChecked: root.alwaysEnabled || root.overrideEnabled
        collapsible: root.collapsible
        onToggleClicked: function(checked) {
            if (checked)
                root.commitOverride();
            else
                settingsController.animationsPage.clearOverride(root.eventPath);
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            // ── Inheritance info ──────────────────────────────────────
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                visible: !root.alwaysEnabled && (root.isParentNode ? root.overrideEnabled : !root.overrideEnabled)
                text: {
                    if (root.isParentNode)
                        return i18n("Settings here apply to all child events unless individually overridden.");

                    var chain = root.parentChainText();
                    if (chain.length > 0)
                        return i18n("Inheriting from: %1", chain);

                    return i18n("Using library defaults");
                }
            }

            // ── Shadowing-children warning (parent-node cards only) ───
            // ShaderProfileTree::resolve walks parent → leaf and overlays
            // each level's `effectId` if engaged; deeper leaves win. So
            // a stale per-leg override from an earlier session (e.g. an
            // old "Layout Picker — Show = dissolve" left over after the
            // user switches to a parent "All Popups = morph") silently
            // overrides the parent at runtime — even though the parent
            // card visually shows "morph" and the user never sees the
            // shadowing leaf. Surface it explicitly with one-click
            // remediation; without the button, the only fix is to find
            // each shadowing leaf manually and clear its override.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Warning
                visible: root.isParentNode && root._shadowingChildrenCount > 0
                text: i18np("%n child event has a shader override that overrides this parent.", "%n child events have shader overrides that override this parent.", root._shadowingChildrenCount)
                actions: [
                    Kirigami.Action {
                        text: i18n("Clear shadowing children")
                        icon.name: "edit-clear-all"
                        onTriggered: {
                            settingsController.animationsPage.clearShaderOverrideDescendants(root.eventPath);
                        }
                    }
                ]
            }

            Label {
                visible: !root.alwaysEnabled && !root.overrideEnabled
                text: i18n("Current: %1", root.inheritSummaryText())
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
            }

            // ── Override controls ─────────────────────────────────────
            ColumnLayout {
                visible: root.alwaysEnabled || root.overrideEnabled
                spacing: Kirigami.Units.smallSpacing

                // Curve summary row: thumbnail + description + "Customize…"
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    CurveThumbnail {
                        id: curveThumbnail

                        implicitWidth: Kirigami.Units.gridUnit * 6
                        implicitHeight: Kirigami.Units.gridUnit * 4
                        curve: root.currentEasingCurve
                        timingMode: root.currentTimingMode
                        omega: root.currentSpringOmega
                        zeta: root.currentSpringZeta
                        onClicked: curveDialog.open()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                        Label {
                            Layout.fillWidth: true
                            text: root.summaryDescription()
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.summarySecondary()
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            color: Kirigami.Theme.disabledTextColor
                            elide: Text.ElideRight
                        }

                    }

                    Button {
                        text: i18n("Customize…")
                        icon.name: "configure"
                        Accessible.name: i18n("Customize curve for %1", root.eventLabel)
                        onClicked: curveDialog.open()
                    }

                }

                SettingsSeparator {
                }

                // ── Timing mode ───────────────────────────────────────
                SettingsRow {
                    title: i18n("Timing mode")

                    WideComboBox {
                        id: timingModeCombo

                        Accessible.name: i18n("Timing mode")
                        model: [i18n("Easing"), i18n("Spring")]
                        currentIndex: root.currentTimingMode
                        onActivated: function(index) {
                            root.currentTimingMode = index;
                            root.commitOverride();
                        }
                    }

                }

                // ── Duration (easing only — spring derives its own settle) ─
                SettingsSeparator {
                    visible: root.currentTimingMode === CurvePresets.timingModeEasing
                }

                SettingsRow {
                    visible: root.currentTimingMode === CurvePresets.timingModeEasing
                    title: i18n("Duration")

                    SettingsSlider {
                        from: settingsController.generalPage.animationDurationMin
                        to: settingsController.generalPage.animationDurationMax
                        stepSize: 10
                        valueSuffix: " ms"
                        Accessible.name: i18n("Animation duration")
                        labelWidth: Kirigami.Units.gridUnit * 4
                        value: root.currentDuration
                        onMoved: function(value) {
                            root.currentDuration = Math.round(value);
                            root.commitOverride();
                        }
                    }

                }

            }

            SettingsSeparator {
                visible: root._shaderLegSupported
            }

            SettingsRow {
                visible: root._shaderLegSupported
                title: i18n("Shader effect")
                description: i18n("Apply a shader transition to this event")

                WideComboBox {
                    // `availableShaderEffects()` is a Q_INVOKABLE — QML's
                    // binding engine can't observe internal state of an
                    // opaque function call, so a plain
                    // `readonly property var _effects: …availableShaderEffects()`
                    // evaluates exactly once and never refreshes. That broke
                    // the saved-shader display at startup: when the registry
                    // is still warming up (XDG scan completes asynchronously
                    // on some setups) the first read returns an empty list,
                    // `_indexOf("glitch")` falls through to 0, and the combo
                    // sticks on "None" forever even after the registry
                    // populates and emits `shaderEffectsChanged`.
                    // Standard binding pattern (mirrors stickyHandlingCombo
                    // in SnappingBehaviorPage): bind currentIndex to the
                    // source-of-truth, write back through the controller in
                    // onActivated. The controller routes through Settings::
                    // setShaderProfileTree → shaderProfileTreeJson Q_PROPERTY
                    // NOTIFY → SettingsController meta-object loop catches
                    // it for dirty tracking.

                    id: shaderCombo

                    // Tying the binding to a counter that ticks on the
                    // controller's `shaderEffectsChanged` signal turns the
                    // function call into a reactive binding without needing
                    // a Q_PROPERTY surface for the effects list.
                    property int _effectsRev: 0
                    readonly property var _effects: {
                        _effectsRev; // re-evaluate when the registry signals
                        return settingsController.animationsPage.availableShaderEffects();
                    }
                    readonly property var _modelLabels: {
                        var labels = [i18n("None")];
                        for (var i = 0; i < _effects.length; i++) labels.push(_effects[i].name || _effects[i].id)
                        return labels;
                    }

                    Accessible.name: i18n("Shader effect")
                    model: _modelLabels
                    // Binding intentionally inlines the lookup rather than
                    // delegating to `_indexOf`: QML's reactive engine can
                    // only re-evaluate when the bindings's expression reads
                    // a tracked property, and a function-call boundary
                    // breaks that tracking on some Qt versions. Inlining
                    // makes both `_effects` (so newly-loaded effects
                    // re-resolve the saved id to its real index) and
                    // `root.currentShaderEffectId` (so refreshShaderFromTree
                    // mutations refresh the combo) load-bearing
                    // dependencies of the binding.
                    currentIndex: {
                        var id = root.currentShaderEffectId;
                        if (!id || id.length === 0)
                            return 0;

                        for (var i = 0; i < _effects.length; i++) {
                            if (_effects[i].id === id)
                                return i + 1;

                        }
                        return 0;
                    }
                    onActivated: function(index) {
                        if (index === 0) {
                            settingsController.animationsPage.clearShaderOverride(root.eventPath);
                        } else {
                            var effect = _effects[index - 1];
                            // Switching to a DIFFERENT effect: drop the
                            // previous effect's parameter map. The new
                            // effect's parameter schema is unrelated, so
                            // carrying the old keys persists dead values
                            // on disk (and the param editor's
                            // paramInitialValue path falls through to
                            // type-defaults anyway because the keys
                            // don't match the new schema). Same effect:
                            // pass through the current map so a no-op
                            // re-pick doesn't wipe in-progress edits.
                            var newParams = (effect.id === root.currentShaderEffectId) ? (root.currentShaderParams || ({
                            })) : ({
                            });
                            settingsController.animationsPage.setShaderOverride(root.eventPath, effect.id, newParams);
                        }
                    }

                    Connections {
                        function onShaderEffectsChanged() {
                            ++shaderCombo._effectsRev;
                        }

                        target: settingsController.animationsPage
                    }

                }

            }

            // Inline parameter editor surfaces only when an effect is
            // assigned and that effect declares parameters.
            AnimationShaderParamEditor {
                Layout.fillWidth: true
                visible: root._shaderLegSupported && effectId.length > 0
                effectId: root.currentShaderEffectId
                currentParams: root.currentShaderParams
                onParamsChanged: function(next) {
                    root.currentShaderParams = next;
                    settingsController.animationsPage.setShaderOverride(root.eventPath, root.currentShaderEffectId, next);
                }
            }

        }

    }

    // Pop the editor as a window-level dialog so it doesn't get clipped
    // by the scrolling Flickable that hosts the card.
    CurveEditorDialog {
        id: curveDialog

        parent: root.Window.window ? root.Window.window.contentItem : root
        eventLabel: root.eventLabel
        timingMode: root.currentTimingMode
        easingCurve: root.currentEasingCurve
        springOmega: root.currentSpringOmega
        springZeta: root.currentSpringZeta
        onCurveApplied: function(curve) {
            root.currentEasingCurve = curve;
            root.currentTimingMode = CurvePresets.timingModeEasing;
            root.commitOverride();
        }
        onSpringApplied: function(omega, zeta) {
            root.currentSpringOmega = omega;
            root.currentSpringZeta = zeta;
            root.currentTimingMode = CurvePresets.timingModeSpring;
            root.commitOverride();
        }
    }

}
