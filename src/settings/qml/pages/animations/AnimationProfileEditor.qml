// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.phosphor.animation
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
 * The shader-parameter sub-editor exposes reset / locking / randomize /
 * colour-picker affordances via the @c enableReset / @c enableLocking /
 * @c enableRandomize / @c enableImage flags.
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
    /// Simple-mode trim: hide the timing-mode machinery (curve summary +
    /// Customize dialog entry, Easing/Spring discriminator) and keep only
    /// the Duration slider alongside the shader picker + parameters. The
    /// stored profile keeps whatever curve/mode it already has.
    property bool simpleTiming: false
    /// Show the per-axis inheritance status captions (with their revert
    /// links) under the curve summary and the Duration row. The per-event
    /// card turns this on so a user can see WHICH of the two timing
    /// fields a direct override actually pins; the Global defaults card
    /// has no inheritance to describe and leaves it off.
    property bool showOverrideStatus: false
    /// Whether the edited event owns a DIRECT curve override (as opposed
    /// to following an ancestor or the Global default). Only rendered
    /// when showOverrideStatus is on; the consumer feeds it from the
    /// stored profile.
    property bool curveOverridden: false
    /// Same, for the duration field.
    property bool durationOverridden: false
    /// Picker model — the consumer hands in
    /// `availableShaderEffects()` (or a registry-tick-bound
    /// equivalent) so the picker stays reactive without this editor
    /// having to subscribe. Reactivity is entirely the consumer's:
    /// re-assigning this property is what re-evaluates the picker.
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
    /// Reset-all-to-defaults, defaulting to `enableRandomize` so it tracks
    /// the same contexts (per-event card on, global-defaults page off).
    property bool enableReset: enableRandomize
    /// Image picker is reserved for shader textures (overlay packs);
    /// animation packs don't use it.
    property bool enableImage: false
    /// Expansion state of the shader section (full description +
    /// parameter editor) — the SAME collapsed-row model as the
    /// decoration chain packs. Collapsed by default; picking a shader
    /// expands it, whole-row click toggles it. UI-only, not persisted.
    property bool shaderSectionExpanded: false
    // ── Computed ────────────────────────────────────────────────────
    /// Whether the shader section has anything to reveal (full
    /// description or a parameter editor). Mirrors the decoration
    /// rows' `expandable` gate: no picked shader, or a picked shader
    /// with neither text nor params, keeps the row-click inert and
    /// the chevron hidden.
    readonly property bool shaderSectionExpandable: shaderEffectId.length > 0 && (shaderDescription.length > 0 || shaderParamSchema.length > 0)
    /// Description of the currently-picked shader, resolved from the
    /// consumer-fed picker model. Empty when nothing is picked or the
    /// pack ships no description.
    readonly property string shaderDescription: {
        if (shaderEffectId.length === 0)
            return "";

        for (var i = 0; i < availableShaders.length; ++i) {
            var e = availableShaders[i];
            if (e && e.id === shaderEffectId)
                return typeof e.description === "string" ? e.description : "";
        }
        return "";
    }
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
    /// Per-axis refinements of `valueChanged`, emitted alongside it (the
    /// specific signal first, then the aggregate). A consumer that
    /// persists per-field overrides (the per-event card) listens to
    /// these so a duration drag writes only the duration field and a
    /// curve edit writes only the curve field, leaving the other field
    /// inheriting. Consumers that commit the whole timing state (the
    /// Global defaults card) keep using `valueChanged`. The timing-mode
    /// combo and the spring editor count as CURVE edits: the mode and
    /// the spring parameters are encoded in the curve wire string.
    signal durationEdited
    signal curveEdited
    /// Revert requests from the per-axis "Revert to inherited" links —
    /// only reachable when `showOverrideStatus` is on. The consumer
    /// clears the corresponding field from the stored override so the
    /// event follows its ancestors (and the Global defaults) again.
    signal curveRevertRequested
    signal durationRevertRequested
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
    /// Randomize all params. The editor rolls a new map (honouring the lock set)
    /// and assigns it to `shaderParams` BEFORE emitting. The signal
    /// payload carries the rolled map so a consumer that wants to
    /// persist (per-event card → controller) doesn't have to re-read
    /// the editor's state — a consumer with randomize disabled (the
    /// global-defaults page) never emits it.
    signal randomizeRequested(var rolled)
    /// Reset all shader params to their schema defaults. Same self-update
    /// contract as `randomizeRequested`: the editor stages the defaults map
    /// onto `shaderParams` before emitting, and the payload carries the map
    /// so the consumer persists it in one batch write.
    signal resetRequested(var defaults)

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
            visible: !root.simpleTiming
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
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }

                // Curve-axis inheritance status. Overriding is per field:
                // editing the curve pins only the curve, so this caption
                // (and its duration twin below) tells the user which of
                // the two fields this event actually owns.
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.showOverrideStatus
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: root.curveOverridden ? i18n("Overridden for this event") : i18n("Following the inherited value")
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                        elide: Text.ElideRight
                    }

                    Kirigami.LinkButton {
                        visible: root.curveOverridden
                        text: i18n("Revert to inherited")
                        font: Kirigami.Theme.smallFont
                        Accessible.name: i18n("Revert curve to inherited")
                        onClicked: root.curveRevertRequested()
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }
            }

            Button {
                text: i18n("Customize…")
                icon.name: "configure"
                Accessible.name: root.eventLabel.length > 0 ? i18n("Customize curve for %1", root.eventLabel) : i18n("Customize curve")
                onClicked: curveDialog.open()
            }
        }

        SettingsSeparator {
            visible: !root.simpleTiming
        }

        // Timing mode — Easing vs Spring discriminator.
        SettingsRow {
            visible: !root.simpleTiming
            title: i18n("Timing mode")

            WideComboBox {
                Accessible.name: i18n("Timing mode")
                model: [i18n("Easing"), i18n("Spring")]
                currentIndex: root.timingMode
                onActivated: function (index) {
                    root.timingMode = index;
                    root.curveEdited();
                    root.valueChanged();
                }
            }
        }

        // Duration (easing only — spring derives its own settle time). The
        // separator leads the row, so it hides with the timing-mode chrome
        // in simple mode — a section must not open with a divider.
        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeEasing && !root.simpleTiming
        }

        // Spring stand-in for the hidden Duration row. A spring derives its
        // own settle time from omega and zeta, so the Duration slider below
        // hides whenever the mode is Spring — without this hint the row just
        // vanishes with no explanation. Both modes need it, with wording
        // matched to what each can actually do about it: simple mode has no
        // curve controls of its own, so it points at the Global card and at
        // Advanced; advanced mode has the Timing mode combo right above.
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            visible: root.timingMode === CurvePresets.timingModeSpring
            // Path-neutral wording in simple mode: the spring can come from
            // the Global defaults card or from an override this event owns,
            // and the hint has to name a way out of both. The Global card is
            // the first stop because it is right there on the same page, and
            // Advanced covers the per-event override the card cannot show.
            text: root.simpleTiming ? i18n("A spring curve is set, so the duration has no effect. Change the curve in Global animation defaults, or switch to Advanced in the sidebar to change it for this event.") : i18n("A spring curve derives its own settle time from its parameters, so there is no duration to set. Switch the timing mode to Easing to use a duration.")
            font.italic: true
            color: Kirigami.Theme.disabledTextColor
            wrapMode: Text.WordWrap
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing
            title: i18n("Duration")
            // Duration-axis twin of the curve status caption above. On
            // the simple page this is the only status shown (the curve
            // chrome is hidden there), which matches what simple mode
            // can edit.
            description: root.showOverrideStatus ? (root.durationOverridden ? i18n("Overridden for this event") : i18n("Following the inherited value")) : ""

            SettingsSlider {
                // Bound to the single source of truth (ConfigDefaults, exposed
                // as CONSTANT props) rather than the literal 50 / 2000 — those
                // are PhosphorAnimation::Limits::Min/MaxAnimationDurationMs and
                // must not be re-typed here where they can silently drift.
                from: settingsController.generalPage.animationDurationMin
                to: settingsController.generalPage.animationDurationMax
                stepSize: 10
                valueSuffix: " ms"
                Accessible.name: i18n("Animation duration")
                labelWidth: Kirigami.Units.gridUnit * 4
                value: root.duration
                onMoved: function (value) {
                    root.duration = Math.round(value);
                    root.durationEdited();
                    root.valueChanged();
                }
            }
        }

        // Escape hatch for a pinned duration: without it the only way
        // to unpin one field is the card's Override toggle, which
        // clears BOTH fields and the shader leg with it.
        RowLayout {
            visible: root.showOverrideStatus && root.durationOverridden && root.timingMode === CurvePresets.timingModeEasing
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Kirigami.LinkButton {
                text: i18n("Revert duration to inherited")
                font: Kirigami.Theme.smallFont
                Accessible.name: i18n("Revert duration to inherited")
                onClicked: root.durationRevertRequested()
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    // ── Shader section ──────────────────────────────────────────────
    SettingsSeparator {
        visible: root.shaderLegSupported && root.showTimingSection
    }

    // Shader header row — the same collapsed-row model as the decoration
    // chain packs (ExpandableRowDelegate + ExpandChevron): whole-row click
    // toggles, the chevron signals state, and the collapsed header shows a
    // one-line elided description teaser while the expansion body carries
    // the full text + parameter editor. Hand-rolled from the same pattern
    // rather than reusing ExpandableRowDelegate, because that shell
    // lazy-loads its body and the parameter editor must stay statically
    // instantiated — the `lockedShaderParams` alias above targets its id.
    ItemDelegate {
        visible: root.shaderLegSupported
        Layout.fillWidth: true
        hoverEnabled: true
        // Match the largeSpacing inset the SettingsRows in this editor
        // self-apply, so the header text and picker line up with them.
        leftPadding: Kirigami.Units.largeSpacing
        rightPadding: Kirigami.Units.largeSpacing
        Accessible.name: i18n("Shader effect")
        onClicked: {
            if (root.shaderSectionExpandable)
                root.shaderSectionExpanded = !root.shaderSectionExpanded;
        }

        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                Label {
                    Layout.fillWidth: true
                    text: i18n("Shader effect")
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: i18n("Apply a shader transition to this event")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }

                // One-line teaser while collapsed; hidden when expanded —
                // the expansion body shows the FULL wrapped description
                // instead, so the text never appears twice and never
                // truncates where the user cannot recover it.
                Label {
                    Layout.fillWidth: true
                    visible: root.shaderDescription.length > 0 && !root.shaderSectionExpanded
                    text: root.shaderDescription
                    opacity: 0.7
                    elide: Text.ElideRight
                }
            }

            PZCommon.CategoryMenuButton {
                id: shaderPicker

                Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                Layout.alignment: Qt.AlignVCenter
                // Bound straight to the consumer's property. The consumer owns the
                // registry-tick dependency and re-assigns this on every bump, so an
                // extra tick read here would only double the invalidation.
                items: root.availableShaders
                currentId: root.shaderEffectId
                noneId: ""
                includeNoneEntry: true
                placeholderText: i18nc("@action:button", "Select shader…")
                onSelected: function (id) {
                    var sid = id || "";
                    root.shaderEffectActivated(sid);
                    // Picking a shader reveals its description + parameters
                    // right away; clearing to None collapses the section.
                    root.shaderSectionExpanded = sid.length > 0;
                }
            }

            ExpandChevron {
                visible: root.shaderSectionExpandable
                expanded: root.shaderSectionExpanded
            }
        }
    }

    // Clipped, animated expansion body — mirrors ExpandableRowDelegate's
    // accordion/fade motion so the shader section collapses exactly like
    // a decoration pack row, but with a statically-declared body (see the
    // header comment for why no Loader).
    Item {
        id: shaderExpansionClip

        // Collapses on any exit path: user toggle, shader cleared to None
        // (expandable drops), or the event's shader leg going unsupported.
        readonly property bool effectiveExpanded: root.shaderSectionExpanded && root.shaderSectionExpandable && root.shaderLegSupported

        Layout.fillWidth: true
        // SettingsRow insets its content by largeSpacing on both sides
        // via internal anchors; this body lays out directly in the
        // ColumnLayout, so match that inset explicitly or the description
        // and Parameters header hug the card's left edge.
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        Layout.preferredHeight: effectiveExpanded ? shaderExpansionBody.implicitHeight : 0
        clip: true
        opacity: effectiveExpanded ? 1 : 0
        visible: Layout.preferredHeight > 0 || opacity > 0

        Behavior on Layout.preferredHeight {
            PhosphorMotionAnimation {
                profile: shaderExpansionClip.effectiveExpanded ? "widget.accordionExpand" : "widget.accordionCollapse"
                durationOverride: Kirigami.Units.shortDuration
            }
        }

        Behavior on opacity {
            PhosphorMotionAnimation {
                profile: shaderExpansionClip.effectiveExpanded ? "widget.fadeIn" : "widget.fadeOut"
                durationOverride: Kirigami.Units.veryShortDuration * 2
            }
        }

        ColumnLayout {
            id: shaderExpansionBody

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: Kirigami.Units.smallSpacing

            // Full wrapped description of the picked shader, matching how
            // the decoration chain rows surface their pack description
            // when expanded (ChainEditor).
            Label {
                Layout.fillWidth: true
                visible: root.shaderDescription.length > 0
                text: root.shaderDescription
                wrapMode: Text.WordWrap
                opacity: 0.7
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
                visible: root.shaderEffectId.length > 0 && _paramSchema.length > 0
                parameters: _paramSchema
                currentValues: root.shaderParams
                effectId: root.shaderEffectId
                enableLocking: root.enableLocking
                enableRandomize: root.enableRandomize
                enableReset: root.enableReset
                enableImage: root.enableImage
                compact: true
                // The shared editor owns the lock map (aliased onto
                // `lockedShaderParams`) and hosts the colour dialog, so only the
                // value-write and randomize signals need handling here. Lock state is
                // working-state only and is not re-emitted: no consumer persists it,
                // and forwarding signals nothing listens to only invites the next
                // reader to hunt for the handler that does.
                onValueChanged: function (effectId, paramId, value) {
                    root.shaderParamWriteRequested(effectId, paramId, value);
                }
                onRandomizeRequested: function (rolled) {
                    // Stage the rolled map so the UI updates before the consumer's
                    // persistence round-trips it back through `shaderParams`.
                    root.shaderParams = rolled;
                    root.randomizeRequested(rolled);
                }
                onResetRequested: function (defaults) {
                    // Same staging as randomize: reflect the defaults immediately,
                    // then hand the map up for the consumer to persist.
                    root.shaderParams = defaults;
                    root.resetRequested(defaults);
                }
            }
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
            root.curveEdited();
            root.valueChanged();
        }
        onSpringApplied: function (omega, zeta) {
            root.springOmega = omega;
            root.springZeta = zeta;
            root.timingMode = CurvePresets.timingModeSpring;
            root.curveEdited();
            root.valueChanged();
        }
    }
}
