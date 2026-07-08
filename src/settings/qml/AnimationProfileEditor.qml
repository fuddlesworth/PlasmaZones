// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief Shared timing + shader editor body used by every page that
 *        builds an animation profile.
 *
 * Owns the working-state properties for both axes (curve / duration
 * for timing, effect id / parameter map for shader) and renders the
 * widget tree the per-event card and the App Rules form both need
 * (CurveThumbnail + Customize button → CurveEditorDialog,
 * timing-mode combo, duration slider, CategoryMenuButton +
 * ShaderParameterEditor + ColorDialog).
 *
 * The editor is intentionally persistence-agnostic: it emits
 * @c valueChanged() whenever any tracked field is touched, and the
 * consumer decides whether to commit live (per-event card) or batch
 * the result on a single button click (App Rules). Properties are
 * fully read-write so a consumer can also seed the editor from disk
 * before showing it.
 *
 * The shader-parameter sub-editor exposes the same locking /
 * randomize / colour-picker affordances as the per-event card via
 * the @c enableLocking / @c enableRandomize / @c enableImage flags;
 * pages that don't want them (App Rules) leave the flags at their
 * defaults.
 */
ColumnLayout {
    id: root

    // ── Working state — full read-write surface ─────────────────────
    /// Easing or spring discriminator. `CurvePresets.timingModeEasing`
    /// or `CurvePresets.timingModeSpring`.
    property int timingMode: CurvePresets.timingModeEasing
    /// Active duration in milliseconds. Spring mode derives its own
    /// settle time, so the slider is hidden in that case but the
    /// property still tracks the easing-side value across mode flips.
    property int duration: CurvePresets.defaultDurationMs
    /// Easing curve in `Profile`'s wire format (`x1,y1,x2,y2`,
    /// `elastic-out:amp,per`, etc.). Drives the easing branch of
    /// `curveString`.
    property string easingCurve: CurvePresets.defaultEasingCurve
    /// Spring (omega, zeta) — see `PhosphorAnimation::Spring`.
    property real springOmega: CurvePresets.defaultSpringOmega
    property real springZeta: CurvePresets.defaultSpringZeta
    /// Currently picked shader effect id from the registry. Empty
    /// string is the documented engaged-blocking sentinel: the rule
    /// or per-event override means "no shader for this event".
    property string shaderEffectId: ""
    /// Current per-effect parameter overrides. Keys are the effect's
    /// `parameters` schema ids; values are the user's overrides.
    property var shaderParams: ({})
    /// Per-card lock state for the parameter editor's lock toolbar.
    /// UI affordance only — not persisted. The per-event card
    /// rewires this on shader switch (same-named ids in different
    /// shader schemas are unrelated); App Rules leaves locking off.
    property var lockedShaderParams: ({})
    // ── Configuration inputs ────────────────────────────────────────
    /// Title for the curve dialog ("Customize Curve: <eventLabel>").
    property string eventLabel: ""
    /// Whether the shader section is rendered at all. The per-event
    /// card sets this false for events whose runtime path doesn't
    /// consume a shader leg (e.g. `panel.slideIn`).
    property bool shaderLegSupported: true
    /// Whether the shader section is rendered (in addition to the
    /// shader-leg gate above). The App Rules page sets this to
    /// `false` when the user picks the Timing-kind radio so the
    /// shader controls collapse out of the form.
    property bool showShaderSection: true
    /// Whether to render the timing section (curve / duration). Per-
    /// event cards always render it; the App Rules page hides it when
    /// the user picks the Shader-kind radio.
    property bool showTimingSection: true
    /// Bumped externally on `shaderEffectsChanged` so this editor's
    /// `shaderParameters(effectId)` schema binding re-evaluates when a
    /// pack is dropped / removed mid-session.
    property int registryRevision: 0
    /// Picker model — the consumer hands in
    /// `availableShaderEffects()` (or a registry-tick-bound
    /// equivalent) so the picker stays reactive without this editor
    /// having to subscribe.
    property var availableShaders: []
    // ── Shader-param editor feature toggles ─────────────────────────
    /// Locking is per-event-card-only — App Rules don't need it.
    property bool enableLocking: false
    /// Randomize same.
    property bool enableRandomize: false
    /// Image picker is reserved for shader textures (overlay packs);
    /// animation packs don't use it.
    property bool enableImage: false
    // ── Inheritance opt-out (App Rules mode) ────────────────────────
    /// When true, render `Override curve` / `Override duration`
    /// checkboxes that gate whether the saved value commits the
    /// engaged-empty / zero sentinel for that axis (so the rule
    /// resolver falls through to the per-event default). The
    /// per-event card leaves this false — its master override toggle
    /// handles the inherit-vs-set decision at a higher level.
    property bool showOverrideCheckboxes: false
    property bool overrideCurve: true
    property bool overrideDuration: true
    // ── Computed ────────────────────────────────────────────────────
    /// Wire-format curve string the rule / profile schema expects.
    readonly property string curveString: {
        if (timingMode === CurvePresets.timingModeSpring)
            return "spring:" + springOmega.toFixed(2) + "," + springZeta.toFixed(2);

        return easingCurve;
    }

    // ── Signals ─────────────────────────────────────────────────────
    /// Emitted on a tracked TIMING-axis change (mode / curve string /
    /// duration / spring params). Shader-axis mutations route through
    /// the dedicated `shaderEffectActivated` and
    /// `shaderParamWriteRequested` signals instead, so consumers can
    /// distinguish a curve edit from a shader switch (which carries
    /// side-effects like dropping the previous effect's params).
    /// Per-event card connects this to its imperative commit path;
    /// App Rules leaves it disconnected (commits on Add click).
    signal valueChanged
    /// Emitted when the user activates a shader from the picker.
    /// Distinct from `valueChanged` so the consumer can persist a
    /// shader switch (which carries side-effects: dropping the
    /// previous effect's params, writing the engaged-empty sentinel
    /// for "None") with full context.
    signal shaderEffectActivated(string id)
    /// Emitted when the user mutates a single shader parameter.
    /// Carries (effectId, paramId, value) so the per-event card can
    /// snapshot the effect at user-action time and skip late writes
    /// against a stale effect.
    signal shaderParamWriteRequested(string effectId, string paramId, var value)
    /// Lock toolbar affordances. The editor self-updates
    /// `lockedShaderParams` before emitting these — consumers only need
    /// to subscribe if they want to persist the lock state somewhere
    /// (the per-event card writes to controller; App Rules holds it in
    /// the working set). Both signals fire AFTER the editor's own
    /// state mutation, so a consumer reading `lockedShaderParams` in
    /// the handler sees the post-toggle map.
    signal lockToggleRequested(string paramId, bool locked)
    signal lockAllToggleRequested(bool locked)
    /// Randomize all params. Same self-update contract as the lock
    /// signals: the editor rolls a new map (honouring the lock set)
    /// and assigns it to `shaderParams` BEFORE emitting. The signal
    /// payload carries the rolled map so a consumer that wants to
    /// persist (per-event card → controller) doesn't have to re-read
    /// the editor's state — staging-only consumers (App Rules) can
    /// ignore the signal entirely.
    signal randomizeRequested(var rolled)

    // ── Helpers ─────────────────────────────────────────────────────
    /// "Easing · Cubic In-Out" / "Spring · Snappy" (or "Custom").
    function summaryDescription() {
        if (timingMode === CurvePresets.timingModeSpring) {
            var si = CurvePresets.springPresetIndex(springOmega, springZeta);
            if (si >= 0)
                return i18n("Spring · %1", CurvePresets.springPresets[si].label);

            return i18n("Spring · Custom");
        }
        var idx = CurvePresets.findIndices(easingCurve);
        if (idx.styleIndex >= 0)
            return CurvePresets.easingStyles[idx.styleIndex].label + " · " + CurvePresets.easingDirections[idx.dirIndex].label;

        return i18n("Easing · Custom");
    }

    /// Smaller secondary line: spring (ω, ζ) or duration.
    function summarySecondary() {
        if (timingMode === CurvePresets.timingModeSpring)
            return i18n("ω=%1 · ζ=%2", springOmega.toFixed(1), springZeta.toFixed(2));

        return i18n("%1 ms", duration);
    }

    // ── Imperative API ──────────────────────────────────────────────
    /// Compute a randomized parameter map without writing it.
    /// The per-event card uses this then routes through its batch
    /// writer so a single setShaderOverride call carries every roll.
    function randomizedShaderParams() {
        return paramEditor.computeRandomized();
    }

    /// Update the lock state after a single-row toggle.
    function lockedAfterToggle(paramId, locked) {
        return paramEditor.lockedAfterToggle(paramId, locked);
    }

    /// Update the lock state after the toolbar's lock-all toggle.
    function lockedAfterAllToggle(locked) {
        return paramEditor.lockedAfterAllToggle(locked);
    }

    spacing: Kirigami.Units.smallSpacing

    // ── Timing section ──────────────────────────────────────────────
    ColumnLayout {
        visible: root.showTimingSection
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        // Curve summary row: thumbnail + description + Customize…
        // The override-curve checkbox sits on the same row so the
        // user can disable the override without losing their working
        // state.
        // Curve override checkbox — own row in App Rules mode so it
        // doesn't crowd the curve summary preview / button below.
        // Hidden entirely in per-event-card mode (the card's master
        // override toggle gates the whole timing section).
        CheckBox {
            Layout.fillWidth: true
            // Inset to match the SettingsRows below (which self-inset by
            // largeSpacing); without it this custom row runs edge-to-edge.
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            visible: root.showOverrideCheckboxes
            text: i18n("Override curve")
            checked: root.overrideCurve
            onToggled: {
                root.overrideCurve = checked;
                root.valueChanged();
            }
            ToolTip.text: i18n("When off, the per-event default curve is used.")
            ToolTip.visible: hovered
        }

        // Curve summary row: thumbnail + description + Customize…
        // Dimmed when the override is off so the user can still see
        // the inherited preview without it competing for visual
        // weight with the active controls.
        RowLayout {
            Layout.fillWidth: true
            // Inset the curve-summary row (thumbnail + description + Customize…)
            // to match the SettingsRows below, which self-inset by largeSpacing.
            // Without it the thumbnail hugs the left edge and the Customize button
            // the right edge of the card.
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing
            opacity: (!root.showOverrideCheckboxes || root.overrideCurve) ? 1 : 0.5
            enabled: !root.showOverrideCheckboxes || root.overrideCurve

            CurveThumbnail {
                id: curveThumbnail

                implicitWidth: Kirigami.Units.gridUnit * 6
                implicitHeight: Kirigami.Units.gridUnit * 4
                curve: root.easingCurve
                timingMode: root.timingMode
                omega: root.springOmega
                zeta: root.springZeta
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
                Accessible.name: root.eventLabel.length > 0 ? i18n("Customize curve for %1", root.eventLabel) : i18n("Customize curve")
                onClicked: curveDialog.open()
            }
        }

        SettingsSeparator {}

        // Timing mode — Easing vs Spring discriminator.
        SettingsRow {
            title: i18n("Timing mode")
            enabled: !root.showOverrideCheckboxes || root.overrideCurve

            WideComboBox {
                Accessible.name: i18n("Timing mode")
                model: [i18n("Easing"), i18n("Spring")]
                currentIndex: root.timingMode
                onActivated: function (index) {
                    root.timingMode = index;
                    root.valueChanged();
                }
            }
        }

        // Duration (easing only — spring derives its own settle time).
        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeEasing
        }

        // Duration override checkbox — same own-row layout as the
        // curve override above. Hidden in per-event-card mode.
        CheckBox {
            Layout.fillWidth: true
            visible: root.showOverrideCheckboxes && root.timingMode === CurvePresets.timingModeEasing
            text: i18n("Override duration")
            checked: root.overrideDuration
            onToggled: {
                root.overrideDuration = checked;
                root.valueChanged();
            }
            ToolTip.text: i18n("When off, the per-event default duration is used.")
            ToolTip.visible: hovered
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing
            title: i18n("Duration")
            enabled: !root.showOverrideCheckboxes || root.overrideDuration

            SettingsSlider {
                from: 50
                to: 2000
                stepSize: 10
                valueSuffix: " ms"
                Accessible.name: i18n("Animation duration")
                labelWidth: Kirigami.Units.gridUnit * 4
                value: root.duration
                onMoved: function (value) {
                    root.duration = Math.round(value);
                    root.valueChanged();
                }
            }
        }
    }

    // ── Shader section ──────────────────────────────────────────────
    SettingsSeparator {
        visible: root.shaderLegSupported && root.showShaderSection && root.showTimingSection
    }

    SettingsRow {
        visible: root.shaderLegSupported && root.showShaderSection
        title: i18n("Shader effect")
        description: i18n("Apply a shader transition to this event")

        PZCommon.CategoryMenuButton {
            id: shaderPicker

            // `availableShaders` is supplied by the consumer; bumping
            // `registryRevision` is how reactivity is communicated.
            readonly property var _effectModel: {
                void (root.registryRevision);
                return root.availableShaders;
            }

            Layout.fillWidth: true
            items: _effectModel
            currentId: root.shaderEffectId
            noneId: ""
            includeNoneEntry: true
            placeholderText: i18nc("@action:button", "Select shader…")
            onSelected: function (id) {
                var sid = id || "";
                root.shaderEffectActivated(sid);
            }
        }
    }

    // Inline parameter editor surfaces only when an effect is
    // assigned and that effect declares parameters.
    PZCommon.ShaderParameterEditor {
        id: paramEditor

        readonly property var _paramSchema: {
            void (root.registryRevision);
            return root.shaderEffectId.length > 0 ? settingsController.animationsPage.shaderParameters(root.shaderEffectId) : [];
        }

        Layout.fillWidth: true
        // SettingsRow insets its content by largeSpacing on both sides
        // via internal anchors; this editor lays out directly in the
        // ColumnLayout, so match that inset explicitly or the Parameters
        // header and rows hug the card's left edge.
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        visible: root.shaderLegSupported && root.showShaderSection && root.shaderEffectId.length > 0 && _paramSchema.length > 0
        parameters: _paramSchema
        currentValues: root.shaderParams
        lockedParams: root.lockedShaderParams
        enableLocking: root.enableLocking
        enableRandomize: root.enableRandomize
        enableGroups: true
        enableImage: root.enableImage
        compact: true
        onValueChanged: function (paramId, value) {
            root.shaderParamWriteRequested(root.shaderEffectId, paramId, value);
        }
        onLockToggled: function (paramId, locked) {
            // Editor owns `lockedShaderParams` — self-update before
            // emitting so subscribers reading the property see the
            // post-toggle map. Consumers that just want to persist the
            // lock state (per-event card) connect to the signal; pure
            // staging consumers (App Rules) need no handler at all.
            root.lockedShaderParams = paramEditor.lockedAfterToggle(paramId, locked);
            root.lockToggleRequested(paramId, locked);
        }
        onLockAllRequested: function (lock) {
            root.lockedShaderParams = paramEditor.lockedAfterAllToggle(lock);
            root.lockAllToggleRequested(lock);
        }
        onRandomizeRequested: {
            // Roll once, stage on the editor (so the UI updates), and
            // emit with the rolled map so a persisting consumer can
            // batch the per-param writes through a single controller
            // call without re-rolling.
            const rolled = paramEditor.computeRandomized();
            root.shaderParams = rolled;
            root.randomizeRequested(rolled);
        }
        onRequestColorPicker: function (paramId, paramName, current) {
            colorDialog.effectId = root.shaderEffectId;
            colorDialog.paramId = paramId;
            colorDialog.paramName = paramName;
            colorDialog.selectedColor = current;
            colorDialog.open();
        }
    }

    // ── Hosted dialogs ──────────────────────────────────────────────
    // Pop the curve editor at window-level so it isn't clipped by the
    // host SettingsFlickable. Same parent-fallback the per-event card
    // used: real Window when available, this editor otherwise during
    // early init.
    CurveEditorDialog {
        id: curveDialog

        parent: root.Window.window ? root.Window.window.contentItem : root
        eventLabel: root.eventLabel
        timingMode: root.timingMode
        easingCurve: root.easingCurve
        springOmega: root.springOmega
        springZeta: root.springZeta
        onCurveApplied: function (curve) {
            root.easingCurve = curve;
            root.timingMode = CurvePresets.timingModeEasing;
            root.valueChanged();
        }
        onSpringApplied: function (omega, zeta) {
            root.springOmega = omega;
            root.springZeta = zeta;
            root.timingMode = CurvePresets.timingModeSpring;
            root.valueChanged();
        }
    }

    // QtQuick.Dialogs.ColorDialog wraps the OS-native colour picker —
    // runs in its own platform window, no `parent` assignment needed
    // (and none accepted). Carries `effectId` so a registry refresh
    // mid-pick can't retarget the write at a different effect's param
    // map (the per-event card consumes that field via the param-write
    // signal handler).
    ColorDialog {
        id: colorDialog

        options: ColorDialog.ShowAlphaChannel

        property string effectId: ""
        property string paramId: ""
        property string paramName: ""

        title: paramName.length > 0 ? i18nc("@title:window", "Choose %1", paramName) : i18nc("@title:window", "Pick color")
        onAccepted: {
            if (paramId === "" || effectId === "")
                return;

            root.shaderParamWriteRequested(effectId, paramId, selectedColor.toString());
        }
    }
}
