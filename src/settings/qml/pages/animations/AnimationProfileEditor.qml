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
 * (@c availableShaders / @c shaderParamSchema) rather than read from a
 * global context, so shader-registry reactivity stays the consumer's.
 *
 * The shader-parameter sub-editor exposes reset / locking / randomize /
 * colour-picker affordances via the @c enableReset / @c enableLocking /
 * @c enableRandomize / @c enableImage flags.
 */
ColumnLayout {
    id: root

    // Collapse outright when every child is hidden (shader-unsupported leaf
    // with the timing editor closed) — an all-hidden ColumnLayout still
    // occupies a spacing slot in the hosting card's column. Same guard the
    // sibling AnimationEventCardBanners documents; the disjunction below is
    // exactly what gates every child.
    visible: root.showTimingSection || root.shaderLegSupported

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
    /// Title for the curve dialog ("Customize curve for <eventLabel>").
    property string eventLabel: ""
    /// Whether the shader section is rendered at all. The per-event
    /// card sets this false for events whose runtime path doesn't
    /// consume a shader leg (e.g. `panel.slideIn`).
    property bool shaderLegSupported: true
    /// Whether to render the timing section (curve / duration). The
    /// per-event card shows it while the timing editor is engaged: a
    /// direct override exists, or the card's toggle latched it open.
    property bool showTimingSection: true
    /// Simple-mode trim: hide the timing-mode machinery (curve summary +
    /// Customize dialog entry, Easing/Spring discriminator) and keep only
    /// the Duration slider alongside the shader picker + parameters. The
    /// stored profile keeps whatever curve/mode it already has.
    property bool simpleTiming: false
    /// Show the per-axis inheritance status captions (with their revert
    /// links) under the curve summary and the Duration row. The per-event
    /// card turns this on so a user can see WHICH of the two timing fields
    /// a direct override actually pins; GlobalTimingDefaultsCard, the other
    /// consumer, has no inheritance to describe and leaves it off.
    property bool showOverrideStatus: false
    /// Whether the edited event owns a DIRECT curve override (as opposed
    /// to following an ancestor or the Global default). Only rendered
    /// when showOverrideStatus is on; the consumer feeds it from the
    /// stored profile.
    property bool curveOverridden: false
    /// Same, for the duration field.
    property bool durationOverridden: false
    /// Same, for the shader slot: true when this event owns a DIRECT pack
    /// choice, false when the pack shown is inherited from an ancestor.
    ///
    /// Worth its own property rather than inferring it from
    /// `shaderEffectId.length`, because that id is the RESOLVED one and an
    /// inherited pack renders through it identically to an owned one. Without
    /// this the row gave the shader axis no ownership cue at all while the two
    /// timing fields above it both had one, and the remove button claimed to
    /// remove a pack this event did not have.
    ///
    /// Engaged-EMPTY does not count: that is the explicit "no shader here"
    /// sentinel, not a pack this event chose. The row is hidden in that state
    /// anyway (it needs a resolved pack to render), which is why the empty
    /// state below carries its own disclosure.
    property bool shaderOwnsPack: false
    /// True when this event inherits its PACK but owns every parameter VALUE.
    ///
    /// A third state rather than a shade of the second, because the storage
    /// really is three-valued and the difference is what the user needs to
    /// know. `ShaderProfile::overlay` replaces the parameter map wholesale
    /// rather than merging keys, so the moment a slider moves on an inheriting
    /// event that event stops following the ancestor's parameter edits while
    /// still following its pack. Reporting that as "Following the inherited
    /// value" understated what the event owns, which is the same class of
    /// mistake the remove button's label made.
    property bool shaderOwnsParamsOnly: false
    /// True when this event stores a shader override of ANY shape, including
    /// the engaged-empty sentinel. Gates the revert affordance, which has to
    /// stay reachable in exactly the state where the pack row is hidden:
    /// after the "no shader" sentinel is written there is no pack to render,
    /// so a control living on the pack row could never undo it.
    property bool shaderOverrideStored: false
    /// Whether the remove control has a pack to remove.
    ///
    /// Distinct from `shaderOwnsPack`, which describes THIS event, because the
    /// consumer may write a GROUP of paths and the control acts on all of them.
    /// A caption is a statement about the event in front of the user; a button
    /// label is a promise about what the click does. Feeding both from the same
    /// property gave a group whose members disagree a label derived from one of
    /// them and an action applied to every one.
    ///
    /// Defaults to tracking `shaderOwnsPack`, so a consumer that writes exactly
    /// one path (which is every consumer but the simple page) gets the right
    /// answer without opting in.
    property bool shaderPackRemovable: shaderOwnsPack
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
    /// Locking is per-event-card-only. A host with no per-event overrides
    /// to protect leaves it off.
    property bool enableLocking: false
    /// Randomize same.
    property bool enableRandomize: false
    /// Reset-all-to-defaults, defaulting to `enableRandomize` so the two
    /// track the same contexts (per-event card on).
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
    readonly property bool shaderSectionExpandable: shaderEffectId.length > 0 && (shaderDescription.length > 0 || (shaderParamSchema || []).length > 0)
    /// Registry entry for the currently-picked shader, resolved from the
    /// consumer-fed picker model. Null when nothing is picked or the id
    /// has no match (a pack uninstalled while its override survives).
    /// Hoisted so the name and description legs share one scan.
    readonly property var _shaderEffect: {
        if (shaderEffectId.length === 0)
            return null;

        // Same untyped-`availableShaders` hazard `_anyPackAvailable` guards
        // below: dereferencing `.length` on an undefined model throws, and the
        // throw takes shaderName, shaderDescription and shaderSectionExpandable
        // down with this binding.
        const list = availableShaders || [];
        for (var i = 0; i < list.length; ++i) {
            var e = list[i];
            if (e && e.id === shaderEffectId)
                return e;
        }
        return null;
    }
    /// Display name of the currently-picked shader. An id with no entry in
    /// the picker model (a pack uninstalled while its override survives, or
    /// one an ancestor set that this event's path filter excludes) renders
    /// with the SAME "(missing: …)" wording CategoryMenuButton uses for the
    /// same state — the picker sits directly below this row, so a bare id
    /// here would read like a real pack name beside the picker's "(missing:
    /// …)" for the very same shader.
    readonly property string shaderName: {
        if (shaderEffectId.length === 0)
            return "";

        if (_shaderEffect && _shaderEffect.name)
            return _shaderEffect.name;

        return i18nc("@info item missing", "(missing: %1)", shaderEffectId);
    }
    /// Description of the currently-picked shader. Empty when nothing is
    /// picked or the pack ships no description.
    readonly property string shaderDescription: (_shaderEffect && typeof _shaderEffect.description === "string") ? _shaderEffect.description : ""
    /// Whether the picker has anything to offer. False while the shader
    /// registry is still loading (or absent), which is a state the empty
    /// slot and the setter caption BOTH have to agree about: telling the
    /// user to set a pack below while the picker holds only "None" is an
    /// instruction they cannot follow.
    // Boolean-coerced: `availableShaders` is untyped, and the `&&` chain
    // returns the first falsy operand, which for an undefined model is
    // `undefined` — a typed bool property rejects that outright.
    readonly property bool _anyPackAvailable: Boolean(availableShaders && availableShaders.length > 0)
    /// The shader row's ownership caption, spelled once because both the
    /// visible Label and the row's `Accessible.description` render it and a
    /// second spelling would drift.
    ///
    /// Three states, because the storage has three. The first two reuse the
    /// wording the curve and duration captions already use, so the axes read
    /// as one convention. The third has no timing counterpart: only the shader
    /// slot can own its values while still inheriting the thing those values
    /// configure.
    readonly property string _shaderOwnershipCaption: {
        if (shaderOwnsPack)
            return i18n("Overridden for this event");

        if (shaderOwnsParamsOnly)
            return i18n("Following the inherited pack, with settings of its own");

        return i18n("Following the inherited value");
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
    /// `GlobalTimingDefaultsCard` is this aggregate's only consumer: it
    /// commits the whole timing state in one go. The per-event card does NOT
    /// connect it — it listens to the per-axis signals below instead, which
    /// is what keeps its writes per field.
    signal valueChanged
    /// Per-axis refinements of `valueChanged`, emitted alongside it (the
    /// specific signal first, then the aggregate). A consumer that
    /// persists per-field overrides (the per-event card) listens to
    /// these so a duration drag writes only the duration field and a
    /// curve edit writes only the curve field, leaving the other field
    /// inheriting. Consumers that commit the whole timing state
    /// (GlobalTimingDefaultsCard) keep using `valueChanged`. The timing-mode
    /// combo and the spring editor count as CURVE edits: the mode and
    /// the spring parameters are encoded in the curve wire string.
    signal durationEdited
    signal curveEdited
    /// Revert requests from the per-axis revert links —
    /// only reachable when `showOverrideStatus` is on. The consumer
    /// clears the corresponding field from the stored override so the
    /// event follows its ancestors (and the Global defaults) again.
    signal curveRevertRequested
    signal durationRevertRequested
    /// The shader-axis twin of the two above: drop whatever this event stores
    /// on the shader slot so it follows its ancestors again.
    ///
    /// Deliberately NOT routed through `shaderEffectActivated("")`. That writes
    /// the engaged-empty sentinel, which is an explicit "no shader for this
    /// event" and BLOCKS the inherited pack. The two are opposite outcomes and
    /// the row now offers both: this one removes the event's own choice, the
    /// picker's None entry blocks what the ancestor gives it.
    signal shaderRevertRequested
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
    /// the editor's state. A host with randomize disabled never emits it.
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
            return i18nc("easing style, then direction", "%1 · %2", CurvePresets.easingStyles[idx.styleIndex].label, CurvePresets.easingDirections[idx.dirIndex].label);

        return i18n("Easing · Custom");
    }

    /// Smaller secondary line: spring (ω, ζ) or duration.
    function summarySecondary() {
        if (timingMode === CurvePresets.timingModeSpring)
            return i18n("ω=%1 · ζ=%2", springOmega.toFixed(1), springZeta.toFixed(2));

        // Bare i18n, not i18nc, and deliberately left that way. A context
        // marker here would read better for translators, but a message is
        // keyed on source PLUS context, so adding one orphans the existing
        // string and `lupdate -no-obsolete` deletes its six approved
        // translations outright — including the Russian one, which is the only
        // one that actually differs from the source. Not worth that for a
        // marker no user sees. The same reasoning is already written down on
        // the "Select a pack…" placeholder below.
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

                    // maximumWidth pins the caption to its natural width so
                    // the link sits directly after it, the way the duration
                    // twin below reads. Without the cap the caption took the
                    // whole row and stranded the link against the Customize
                    // button. A long translation still shrinks and elides
                    // rather than pushing the link out of the card, because
                    // Layout.minimumWidth defaults to 0 and the layout is
                    // free to squeeze below the preferred width; fillWidth
                    // plays no part in that and is deliberately not set.
                    Label {
                        Layout.maximumWidth: implicitWidth
                        text: root.curveOverridden ? i18n("Overridden for this event") : i18n("Following the inherited value")
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                        elide: Text.ElideRight
                    }

                    // Named for its axis, like the duration twin below: the
                    // two links sit a few rows apart and a bare "Revert to
                    // inherited" beside "Revert duration to inherited" reads
                    // as though it reverted everything.
                    Kirigami.LinkButton {
                        // Never squeezed: with no minimum the layout spreads
                        // an overflow across both items, and the actionable
                        // half can elide before the informational half does.
                        // The caption is the one that gives way, and it
                        // already elides.
                        Layout.minimumWidth: implicitWidth
                        visible: root.curveOverridden
                        text: i18n("Revert curve to inherited")
                        font: Kirigami.Theme.smallFont
                        Accessible.name: i18n("Revert curve to inherited")
                        onClicked: root.curveRevertRequested()
                    }

                    // Soaks up the remainder so the pair above stays
                    // left-aligned under the curve summary.
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
        //
        // Gated on simple mode ALONE, not on the timing mode. Exactly one of
        // the Duration row and the spring hint below occupies this slot, and
        // the divider leads whichever it is. Testing for Easing here left the
        // spring hint butting straight against the timing-mode row with no
        // break in advanced mode.
        SettingsSeparator {
            visible: !root.simpleTiming
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
                // These are GLOBAL app limits, not per-profile or per-scope
                // state, so the app-wide settingsController.generalPage is the
                // correct source — there is no context-local controller to
                // prefer, unlike the per-scope appSettings cards elsewhere.
                from: settingsController.generalPage.animationDurationMin
                to: settingsController.generalPage.animationDurationMax
                stepSize: 10
                // The separating space is deliberately OUTSIDE the translatable
                // string: a leading space is invisible in every CAT tool, so a
                // translator trimming it would silently render "150ms".
                valueSuffix: " " + i18nc("milliseconds, unit appended to a slider value", "ms")
                accessibleName: i18n("Animation duration")
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
        // clears BOTH fields and the shader leg with it. Deliberately
        // NOT gated on easing mode: a pinned duration under an
        // inherited spring is inert but still stored, and it silently
        // re-applies the moment the spring goes away upstream, so the
        // way out has to stay reachable while the Duration row itself
        // is hidden.
        //
        // Nor is it hidden in simple mode, which is what living outside the
        // curve-summary block buys: simple mode CAN pin a duration (the
        // slider above is its only timing control), so it needs the way
        // back. It cannot pin a curve: the timing-mode combo sits inside the
        // hidden block, and so do the only two ways into the curve dialog
        // (the thumbnail and Customize…), and an invisible item takes no
        // clicks. So the curve link stays in there with them, in Advanced,
        // where such an override can only have been made.
        RowLayout {
            visible: root.showOverrideStatus && root.durationOverridden
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Kirigami.LinkButton {
                // Matches the curve twin's floor: nothing shares this row that
                // could be squeezed instead, but the two links are read side by
                // side and an asymmetry here reads as an oversight.
                Layout.minimumWidth: implicitWidth
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

    // Empty state, mirroring the decoration chain editor's: an unset
    // shader says so in place of the row, and points at the setter below.
    //
    // Two different states render empty and they are NOT the same thing, so
    // they do not read the same. Nothing stored means the event simply has no
    // pack. The engaged-empty sentinel means the event was explicitly set to
    // no shader, which also blocks whatever an ancestor would have given it —
    // and because the pack row needs a resolved pack to render, that state has
    // no row of its own. Without this disclosure it looked identical to "no
    // pack anywhere", with nothing to say why an ancestor's pack was not
    // reaching the event and no way to undo it.
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        visible: root.shaderLegSupported && root.shaderEffectId.length === 0
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            // Points at the setter only when the setter can actually serve.
            text: {
                if (root.shaderOverrideStored)
                    return i18n("No shader for this event, and no inherited one either.");

                return root._anyPackAvailable ? i18n("No shader pack. Set one below.") : i18n("No shader pack.");
            }
            font: Kirigami.Theme.smallFont
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.disabledTextColor
        }

        // Named for its axis, like the curve and duration links above: a bare
        // "Revert to inherited" a few rows from "Revert duration to inherited"
        // reads as though it reverted everything.
        Kirigami.LinkButton {
            visible: root.showOverrideStatus && root.shaderOverrideStored
            text: i18n("Revert shader to inherited")
            Accessible.name: text
            onClicked: root.shaderRevertRequested()
        }
    }

    // Selected-shader row — the same collapsed-row model as a decoration
    // chain pack (ExpandableRowDelegate + ExpandChevron): the row names the
    // PACK, whole-row click toggles, the chevron signals state, and the
    // collapsed header shows a one-line elided description teaser while the
    // expansion body carries the full text + parameter editor. Hand-rolled
    // from the same pattern rather than reusing ExpandableRowDelegate,
    // because that shell lazy-loads its body and the parameter editor must
    // stay statically instantiated — the `lockedShaderParams` alias above
    // targets its id. Unlike the decoration chain this slot holds at most
    // one pack, so the picker lives in its own setter row below.
    ItemDelegate {
        visible: root.shaderLegSupported && root.shaderEffectId.length > 0
        Layout.fillWidth: true
        hoverEnabled: true
        // Match the largeSpacing inset the SettingsRows in this editor
        // self-apply, so the row's text lines up with them.
        leftPadding: Kirigami.Units.largeSpacing
        rightPadding: Kirigami.Units.largeSpacing
        Accessible.name: root.shaderName
        // The ownership cue reaches assistive tech here rather than from the
        // caption Label below, which a screen reader has no reason to
        // associate with this row. Without it the sighted user learned whether
        // the pack was the event's own and the AT user did not.
        Accessible.description: root.showOverrideStatus ? root._shaderOwnershipCaption : ""
        onClicked: {
            if (root.shaderSectionExpandable)
                root.shaderSectionExpanded = !root.shaderSectionExpanded;
        }

        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: root.shaderName
                    font.bold: true
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
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }

                // The ownership cue the two timing fields above this row
                // already carry, in their wording where the state is the same
                // one they can be in. Applied to the description Label above as
                // well as here, because this column is spacing:0 and a single
                // small line under two body-size ones reads as a mistake.
                Label {
                    Layout.fillWidth: true
                    visible: root.showOverrideStatus
                    text: root._shaderOwnershipCaption
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }
            }

            // Removing the event's own pack, in the same position a decoration
            // pack row carries its remove button.
            //
            // The two arms are genuinely different WRITES, not one write with
            // two labels, because "remove" and "use nothing" are opposite
            // outcomes here. With a pack of its own, removing it lets the
            // ancestor's pack (or the built-in default) reach the event again.
            // With the pack inherited there is nothing of the event's own to
            // remove, so the only meaningful action left is the engaged-empty
            // sentinel, which blocks what the ancestor gives it. Routing both
            // through the sentinel is what made "Remove <pack>" a lie: it told
            // the user they were dropping their own choice when the click was
            // actually refusing everyone else's.
            //
            // Blocking an inherited pack from the owned state is still
            // reachable, through the picker's None entry, which is documented
            // below as the route that must never be gated away.
            ToolButton {
                id: shaderRemoveButton

                // One source for the label so the accessible name and the
                // tooltip cannot drift, and so neither depends on reading the
                // other's attached property.
                readonly property string actionLabel: root.shaderPackRemovable ? i18n("Remove %1", root.shaderName) : i18n("Use no shader for this event")

                icon.name: "edit-delete-remove"
                display: ToolButton.IconOnly
                Accessible.name: shaderRemoveButton.actionLabel
                ToolTip.visible: hovered
                ToolTip.delay: Kirigami.Units.toolTipDelay
                ToolTip.text: shaderRemoveButton.actionLabel
                onClicked: {
                    // Snapshotted so the collapse below can tell a write that
                    // landed from one the consumer refused. Both signals are
                    // handled synchronously and refresh `shaderEffectId` before
                    // returning.
                    const shownBefore = root.shaderEffectId;
                    if (root.shaderPackRemovable)
                        root.shaderRevertRequested();
                    else
                        root.shaderEffectActivated("");
                    // Collapse only when the slot actually moved. A refused
                    // write leaves the same pack on the row, and folding away
                    // its parameters underneath the user is not what "nothing
                    // happened" should look like. Reverting to an ancestor pack
                    // that happens to be the same one is the other case this
                    // catches: the row still shows what it showed, so it stays
                    // as the user left it.
                    if (root.shaderEffectId !== shownBefore)
                        root.shaderSectionExpanded = false;
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
        // Non-interactive the instant it starts collapsing, not when the ramp
        // finishes: without this a click lands on the fading body during the
        // collapse animation while it is still visible but on its way out.
        enabled: effectiveExpanded

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
                color: Kirigami.Theme.disabledTextColor
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
                visible: root.shaderEffectId.length > 0 && (_paramSchema || []).length > 0
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

    // Divider between the selected shader and the setter row, so the
    // picker reads as a separate affordance rather than running into the
    // row it replaces (the same break the decoration chain draws above
    // its add row).
    SettingsSeparator {
        Layout.topMargin: Kirigami.Units.smallSpacing
        visible: root.shaderLegSupported && root.shaderEffectId.length > 0
    }

    // ── Setter row ──────────────────────────────────────────────────
    // Compact right-aligned picker in a labelled SettingsRow, matching the
    // decoration chain's add row. The difference is the slot: this holds a
    // single shader, so picking REPLACES the row above (and None clears
    // it) rather than appending, and the button carries the current
    // selection instead of a permanent placeholder.
    //
    // The button deliberately carries NO `enabled` gate where the decoration
    // add row disables itself on an empty candidate list. That row is a pure
    // append action, so an empty list leaves it nothing to do, while this
    // picker's None entry stays meaningful with an empty catalog: it is how a
    // user blocks a shader inherited from an ancestor path. Gating it on
    // `availableShaders` would take away the only picker route to that.
    SettingsRow {
        id: setShaderRow

        visible: root.shaderLegSupported
        title: i18n("Set shader pack")
        // The caption tracks what the picker can currently do, the way the
        // decoration add row's does: a filled slot says the pick replaces
        // what is already there, an empty one invites a pack, and an empty
        // registry says so instead of promising an action with nothing to
        // offer.
        description: {
            if (root.shaderEffectId.length > 0)
                return i18n("Swap this event's pack for another, or clear it");

            if (!root._anyPackAvailable)
                return i18n("No shader packs are available for this event");

            return i18n("Apply a shader pack to this event");
        }

        PZCommon.CategoryMenuButton {
            // SettingsRow lays its default children out in a plain Row
            // positioner, so Layout.* attached properties are inert here —
            // size explicitly or the button sits at implicit width. Clamped
            // to the same 45% of the row that SettingsRow caps its control
            // slot at, measured against the INNER layout (inset by
            // largeSpacing per side) so it cannot overhang the margin.
            width: Math.min(Kirigami.Units.gridUnit * 16, Math.max(0, (setShaderRow.width - Kirigami.Units.largeSpacing * 2) * 0.45))
            // Bound straight to the consumer's property. The consumer owns the
            // registry-tick dependency and re-assigns this on every bump, so an
            // extra tick read here would only double the invalidation.
            items: root.availableShaders
            currentId: root.shaderEffectId
            noneId: ""
            includeNoneEntry: true
            // Context marker left as @action:button deliberately. It is arguably the
            // wrong register for a placeholder, but the marker is translator-facing
            // only, and changing it invalidates the approved translation of this
            // string in six languages for no user-visible gain.
            placeholderText: i18nc("@action:button", "Select a pack…")
            Accessible.description: i18n("Set the shader pack this event uses")
            onSelected: function (id) {
                var sid = id || "";
                root.shaderEffectActivated(sid);
                // Picking a shader reveals its description + parameters right
                // away, and clearing to None collapses the section. Derived
                // from what actually LANDED rather than from what was asked
                // for: the consumer refreshes `shaderEffectId` synchronously
                // inside the signal, and a write it refused (an async discard
                // owns the tree) leaves the id where it was. Reading `sid` here
                // left the old pack's description and parameters expanded under
                // a row that had not changed.
                root.shaderSectionExpanded = root.shaderEffectId.length > 0;
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
        duration: root.duration
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
