// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief Timing + shader editor body for the per-event animation card.
 *
 * Owns the working-state properties for both axes (curve / duration
 * for timing, effect id / parameter map for shader) and renders the
 * widget tree the per-event card needs (CurveThumbnail + Customize
 * button → CurveEditorDialog, timing-mode combo, duration slider,
 * CategoryMenuButton + ShaderParamsEditor — the shared editor + colour
 * dialog + lock / randomize host).
 *
 * The editor emits @c valueChanged() whenever any tracked field is
 * touched; the consumer commits live. Properties are fully read-write
 * so the consumer can seed the editor from disk before showing it. The
 * picker model and parameter schema are fed in by the consumer
 * (@c availableShaders / @c shaderParamSchema) so the editor doesn't
 * reach a global context itself.
 *
 * The shader-parameter sub-editor exposes locking / randomize /
 * colour-picker affordances via the @c enableLocking / @c enableRandomize
 * / @c enableImage flags.
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
    /// Aliased onto the shared ShaderParamsEditor's `lockedParams`, which
    /// owns the lock map and self-updates it; assigning here (the card's
    /// reset-on-shader-change) writes straight through.
    property alias lockedShaderParams: paramEditor.lockedParams
    // ── Configuration inputs ────────────────────────────────────────
    /// Title for the curve dialog ("Customize Curve: <eventLabel>").
    property string eventLabel: ""
    /// Whether the shader section is rendered at all. The per-event
    /// card sets this false for events whose runtime path doesn't
    /// consume a shader leg (e.g. `panel.slideIn`).
    property bool shaderLegSupported: true
    /// Whether to render the timing section (curve / duration). The
    /// per-event card hides it when its master override toggle is off.
    property bool showTimingSection: true
    /// Bumped externally on `shaderEffectsChanged` so this editor's
    /// consumer-fed picker / schema bindings re-evaluate when a pack is
    /// dropped / removed mid-session.
    property int registryRevision: 0
    /// Picker model — the consumer hands in
    /// `availableShaderEffects()` (or a registry-tick-bound
    /// equivalent) so the picker stays reactive without this editor
    /// having to subscribe.
    property var availableShaders: []
    /// Parameter schema for the currently-picked shader — the consumer
    /// hands in `shaderParameters(shaderEffectId)` (registry-tick-bound)
    /// so the editor doesn't reach a global context for it.
    property var shaderParamSchema: []
    // ── Shader-param editor feature toggles ─────────────────────────
    /// Locking is per-event-card-only — the global-defaults page doesn't need it.
    property bool enableLocking: false
    /// Randomize same.
    property bool enableRandomize: false
    /// Image picker is reserved for shader textures (overlay packs);
    /// animation packs don't use it.
    property bool enableImage: false
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
    /// The per-event card and the global-defaults page (AnimationsGeneralPage)
    /// each connect this to their own commit path.
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
    /// (the per-event card writes to controller; the global-defaults page
    /// leaves locking off). Both signals fire AFTER the editor's own
    /// state mutation, so a consumer reading `lockedShaderParams` in
    /// the handler sees the post-toggle map.
    signal lockToggleRequested(string paramId, bool locked)
    signal lockAllToggleRequested(bool locked)
    /// Randomize all params. Same self-update contract as the lock
    /// signals: the editor rolls a new map (honouring the lock set)
    /// and assigns it to `shaderParams` BEFORE emitting. The signal
    /// payload carries the rolled map so a consumer that wants to
    /// persist (per-event card → controller) doesn't have to re-read
    /// the editor's state — a consumer with randomize disabled (the
    /// global-defaults page) never emits it.
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

    spacing: Kirigami.Units.smallSpacing

    // ── Timing section ──────────────────────────────────────────────
    ColumnLayout {
        visible: root.showTimingSection
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        // Curve summary row: thumbnail + description + Customize…
        RowLayout {
            Layout.fillWidth: true
            // Inset the curve-summary row (thumbnail + description + Customize…)
            // to match the SettingsRows below, which self-inset by largeSpacing.
            // Without it the thumbnail hugs the left edge and the Customize button
            // the right edge of the card.
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

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

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing
            title: i18n("Duration")

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
        visible: root.shaderLegSupported && root.showTimingSection
    }

    SettingsRow {
        visible: root.shaderLegSupported
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
    PZCommon.ShaderParamsEditor {
        id: paramEditor

        // Schema is consumer-fed (root.shaderParamSchema) so this editor
        // doesn't reach a global context; the consumer binds it to
        // shaderParameters(shaderEffectId) with a registry-tick dependency.
        readonly property var _paramSchema: root.shaderParamSchema

        Layout.fillWidth: true
        visible: root.shaderLegSupported && root.shaderEffectId.length > 0 && _paramSchema.length > 0
        parameters: _paramSchema
        currentValues: root.shaderParams
        effectId: root.shaderEffectId
        enableLocking: root.enableLocking
        enableRandomize: root.enableRandomize
        enableImage: root.enableImage
        compact: true
        // The shared editor owns the lock map (aliased onto
        // `lockedShaderParams`) and hosts the colour dialog, so only the
        // value-write and randomize signals need handling here. The lock
        // signals are re-emitted for API parity; current consumers ignore
        // them (lock state is working-state only).
        onValueChanged: function (effectId, paramId, value) {
            root.shaderParamWriteRequested(effectId, paramId, value);
        }
        onLockToggled: function (paramId, locked) {
            root.lockToggleRequested(paramId, locked);
        }
        onLockAllRequested: function (locked) {
            root.lockAllToggleRequested(locked);
        }
        onRandomizeRequested: function (rolled) {
            // Stage the rolled map so the UI updates before the consumer's
            // persistence round-trips it back through `shaderParams`.
            root.shaderParams = rolled;
            root.randomizeRequested(rolled);
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
}
