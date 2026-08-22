// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable card for per-event animation configuration.
 *
 * Each card edits one event in the `PhosphorAnimation::ProfilePaths`
 * taxonomy (e.g. `editor.snapIn`, `osd.show`). Overrides are PER FIELD:
 * editing the duration writes only the duration field and editing the
 * curve writes only the curve field into this event's Profile JSON file
 * under `~/.local/share/plasmazones/profiles/`, so the untouched field
 * keeps following the parent chain and the Global defaults. Flipping the
 * Override toggle ON just opens the timing editor (nothing is written
 * until a control is actually edited); flipping it OFF deletes the
 * override file. The daemon's `ProfileLoader` watches that dir and live-reloads
 * the registry, and the settings app runs one of its own over the same dir, so
 * an edit made outside this page reaches every card as a tree-wide
 * `overrideChanged("")` broadcast.
 *
 * The shader axis follows the same per-field principle through a different
 * store. It lives in the ShaderProfileTree rather than in a profile file, and
 * it splits into TWO fields: which pack the event uses, and the parameter
 * values for it. Moving a parameter slider writes only the parameters, so an
 * event that inherits its pack keeps inheriting it and follows the parent when
 * the parent's pack changes. Picking from the picker is what writes the pack.
 * The one asymmetry with timing is worth knowing: parameter values are stored
 * as a whole map rather than per key, so once an event owns any of them it
 * owns all of them and stops following the parent's parameter edits. The
 * shader row's ownership caption is where that is disclosed.
 *
 * Controls: timing-mode (Easing/Spring), curve thumbnail with
 * "Customize…" dialog, duration slider, inheritance breadcrumb, and — on
 * shader-supported paths — the shader picker and its per-shader parameter
 * editor (both wired through the shared AnimationProfileEditor). Setting
 * `simpleTiming` trims that down to duration, shader and shader parameters.
 *
 * Required properties:
 *   - eventPath:  full path string from `ProfilePaths::` (e.g. "editor.snapIn")
 *   - eventLabel: human-readable label
 *
 * Optional properties:
 *   - isParentNode: bool — flips the inheritance banner copy
 *   - collapsible: bool — header click collapses the body
 *   - simpleTiming: bool — hides the timing-mode combo and the curve editor
 *     (the per-field write rule already keeps a duration-only edit from
 *     pinning the inherited curve; simple mode just trims the chrome)
 *   - mirrorPaths: list<string> — extra event paths every write is echoed to,
 *     so one card can front several events (open mirrored onto close)
 *
 * The `current*` and `locked*` aliases, and `overrideEnabled`, are public only
 * because the aliases have to resolve and the toggle state is derived. Do not
 * assign them from outside: the card seeds them from the profile tree and
 * commits them back, so an outside write is either overwritten on the next
 * refresh or persisted as a user edit.
 */
Item {
    // The curve editor and colour picker live in the shared
    // `AnimationProfileEditor`, so when the editor is hidden so are its dialogs.
    // This card hosts none of its own.

    id: root

    required property string eventPath
    required property string eventLabel
    property bool isParentNode: false
    property bool collapsible: false
    /// Simple-mode trim, forwarded to the embedded AnimationProfileEditor:
    /// hides the timing-mode machinery, keeping duration + shader + params.
    property bool simpleTiming: false
    /// Extra event paths that receive every write this card makes, so one
    /// card can drive a group of analogous events (e.g. a combined
    /// "opened & closed" card writing both window.appearance.open and
    /// .close). The card still READS from `eventPath` alone — the mirrors
    /// are write-only followers.
    ///
    /// Mirroring is intrinsic to the write, deliberately NOT implemented by
    /// observing profile-change signals: the shader signal is a path-agnostic
    /// broadcast, so an observer cannot tell a user edit on this card from an
    /// unrelated card's edit, a Discard, or a profile switch, and would clobber
    /// divergent mirror values. Writing through the call the user's action
    /// triggers has none of those failure modes.
    /// Deliberately `var` (a JS array), not `list<string>`: _writePaths does
    /// `[eventPath].concat(mirrorPaths)`, and Array.prototype.concat SPREADS
    /// only true JS arrays. A QML list proxy would be appended as one element,
    /// silently turning every mirror write into a write to a bogus path.
    property var mirrorPaths: []

    /// True while _setOverrideMerged is writing, so the overrideChanged each
    /// write emits does not re-enter refreshFromTree mid-loop.
    property bool _committing: false

    /// The shader-axis counterpart, for every writer that moves the shader
    /// tree (_setShaderOverrideOnAll, _setShaderParamsOnAll and the two
    /// shader clears).
    property bool _committingShader: false

    /// "The controller refused the last write, so stop re-issuing it."
    ///
    /// Every group writer returns whether its write landed, and the refusal
    /// case is an async discard owning the tree. The controller TOASTS that
    /// reason on every refusal, and a toast announces itself to assistive tech
    /// on every show. Both continuous edit paths run at pointer rate — a
    /// duration drag emits per move, and so does a shader parameter drag — so a
    /// discard that overlapped a drag produced one refused write, one toast and
    /// one screen-reader announcement per pointer move for the length of the
    /// drag. The toast's own animation coalesces visually, which is why this
    /// has to be fixed at the source of the repeat rather than at the toast.
    ///
    /// Cleared in two places, and it needs both. A refresh this card did not
    /// drive itself covers an outside edit; `onPendingChangesChanged` below
    /// covers the end of the discard. The second is NOT redundant: the
    /// discard's terminal handler emits `overrideChanged` only for the profile
    /// files it actually restored, so a card none of those paths reaches would
    /// otherwise never refresh and would stay latched for the rest of the
    /// session. A card's own writes refresh with `selfDriven` set, so the latch
    /// survives the drag that tripped it.
    property bool _writesRefused: false

    /// Record whether a write landed. Takes the boolean every group writer
    /// already returns and that every drag-rate caller used to discard.
    function _noteWriteResult(landed) {
        root._writesRefused = !landed;
    }

    /// The declared mirrors minus any the controller rejects as an event path.
    /// A misspelled entry is refused by every writer, so it can never receive
    /// an edit and its stored state can never match the primary's — which
    /// latches the divergence banner on with no control able to clear it.
    /// Dropping it here is what actually prevents that; Component.onCompleted
    /// warns so the drop is not silent.
    /// `|| []` because mirrorPaths is deliberately untyped `var`, so a consumer
    /// can leave it undefined.
    readonly property var _validMirrorPaths: {
        const declared = root.mirrorPaths || [];
        var kept = [];
        for (var i = 0; i < declared.length; ++i) {
            if (settingsController.animationsPage.isValidEventPath(declared[i]))
                kept.push(declared[i]);
        }
        return kept;
    }
    /// Every path this card writes: its own, then the surviving mirrors.
    readonly property var _writePaths: [root.eventPath].concat(root._validMirrorPaths)
    /// The card's hosting SettingsCard. The virtualized card list re-registers
    /// its search anchor against this once the card builds, so a deep-link
    /// reveal can expand the card when it's collapsed.
    readonly property Item settingsCard: card
    // ── Internal state — one source of truth per UX axis ────────────
    // The on-disk schema is `Profile::toJson()` — a single `curve`
    // string that's either an easing wire format ("x1,y1,x2,y2",
    // "elastic-out:amp,per", etc.) or a spring ("spring:omega,zeta").
    // The card unpacks it on read into the working state below, and
    // re-packs on write. Easing and spring values are remembered
    // independently across timing-mode toggles so the user doesn't
    // lose their easing curve when previewing spring physics.
    property bool overrideEnabled: false
    /// Session-local "the user opened the timing editor" latch. Flipping the
    /// card's Override toggle ON sets this and writes NOTHING — a direct
    /// override is only created when the user actually edits a control, and
    /// then only for the field they edited. Without the latch the toggle had
    /// to commit a snapshot of the inherited values just to stay visually on
    /// (overrideEnabled is derived from stored state), which silently pinned
    /// a copy of the Global curve and duration the moment it was flipped.
    /// Set by the toggle's ON path AND by a successful per-field revert (which
    /// would otherwise collapse the editor the user is working in, once the
    /// last stored field goes). Cleared by the toggle's OFF path, and by
    /// `refreshFromTree` when an EXTERNAL clear moves `overrideEnabled` from
    /// true to false — a transition, not just a false reading, because the
    /// broadcasts every card accepts would otherwise close an editor another
    /// card's edit had nothing to do with. Not persisted.
    property bool _editingTiming: false
    /// "The timing editor is open." Spelled once, because it drives the toggle,
    /// the section's visibility AND both edit guards — a fifth site that
    /// remembered only one half would write while the section was hidden.
    readonly property bool _timingEditorOpen: root.overrideEnabled || root._editingTiming
    /// This path's own stored TIMING profile, assigned by refreshFromTree from
    /// the read it already performs (so no extra file open). Drives the
    /// per-field status captions and the ownership tests below.
    ///
    /// Only the PRIMARY path's, not a map over the whole write group. The group
    /// writers used to need every path's stored profile to merge over, and
    /// cached them here to stay off the file-open path on a drag tick; that
    /// merge now happens C++-side against its own memoised reads, so the cache
    /// and its per-path read loop are gone with it.
    ///
    /// Not `readonly` because it is assigned rather than derived, but
    /// `refreshFromTree` is its only writer — an outside assignment is
    /// overwritten on the next refresh, like every other derived value here.
    property var _primaryRaw: ({})
    /// Whether this event DIRECTLY owns each timing field. Matches the
    /// presence tests the resolver uses: any engaged duration counts, and a
    /// curve counts only as a non-empty string.
    readonly property bool _ownsDurationOverride: root._primaryRaw.duration !== undefined
    readonly property bool _ownsCurveOverride: typeof root._primaryRaw.curve === "string" && root._primaryRaw.curve.length > 0
    /// This path's own STORED shader profile, assigned by refreshFromTree from
    /// the `rawShaderProfile` read it already performs. The shader-axis twin of
    /// `_primaryRaw` above, and not `readonly` for the same reason: it is
    /// assigned rather than derived, with refreshFromTree its only writer.
    ///
    /// Kept as the raw map so the three ownership tests below are DERIVED from
    /// it rather than each being assigned separately. The imperative form would
    /// have re-created, three times over, the staleness the comment on
    /// refreshShaderFromTree warns about: a future caller running that function
    /// alone would leave every one of them describing the previous state.
    property var _primaryRawShader: ({})
    /// Whether this event DIRECTLY owns its shader pack, as opposed to showing
    /// one inherited from an ancestor. The id the editor renders is the
    /// RESOLVED one and cannot answer this on its own.
    ///
    /// Engaged-EMPTY is deliberately NOT ownership: that is the explicit "no
    /// shader here" sentinel, which is a refusal rather than a pack this event
    /// chose. `_storesShaderOverride` is what covers that state.
    readonly property bool _ownsShaderPack: typeof root._primaryRawShader.effectId === "string" && root._primaryRawShader.effectId.length > 0
    /// Whether this event inherits its pack but owns every parameter VALUE —
    /// what a parameter slider writes on an inheriting event.
    readonly property bool _ownsShaderParamsOnly: !root._ownsShaderPack && Boolean(root._primaryRawShader.parameters) && Object.keys(root._primaryRawShader.parameters).length > 0
    /// Whether this event stores a shader override of ANY shape, sentinel
    /// included. Gates the revert affordance, which has to reach the state
    /// where the pack row is not rendered at all.
    readonly property bool _storesShaderOverride: Object.keys(root._primaryRawShader).length > 0
    /// Whether this event holds the engaged-EMPTY sentinel specifically: it
    /// resolves to no shader AND blocks what an ancestor would have given it.
    /// Narrower than `_storesShaderOverride`, because a params-only override
    /// also renders empty once its ancestor's pack goes away and that is a
    /// different thing to tell the user.
    readonly property bool _shaderBlocksInherited: typeof root._primaryRawShader.effectId === "string" && root._primaryRawShader.effectId.length === 0
    /// True when ANY path in the write group owns a pack, which is what the
    /// row's remove control keys on.
    ///
    /// Separate from `_ownsShaderPack` because the caption describes THIS event
    /// while that control WRITES the whole group, and a group whose members
    /// disagree would otherwise get a label derived from one member and an
    /// action applied to all of them. Assigned by refreshFromTree from a single
    /// controller call rather than derived, because the answer lives in the
    /// shader tree and only C++ can read the group in one pass.
    property bool _anyWritePathOwnsShaderPack: false
    // ── Editor-owned working state (proxied via aliases) ────────────
    // The shared `AnimationProfileEditor` (declared inside the card's
    // body below) owns the timing + shader working state and the
    // dialogs / widgets that drive them. This card's existing logic
    // (`refreshFromTree`, the per-axis commits, `_writeShaderParam`, ...)
    // reads / writes these properties unchanged — the aliases keep
    // those call sites working while collapsing the per-event editor
    // body into a single shared component.
    property alias currentTimingMode: editor.timingMode
    property alias currentDuration: editor.duration
    property alias currentEasingCurve: editor.easingCurve
    property alias currentSpringOmega: editor.springOmega
    property alias currentSpringZeta: editor.springZeta
    property alias currentShaderEffectId: editor.shaderEffectId
    property alias currentShaderParams: editor.shaderParams
    property alias lockedShaderParams: editor.lockedShaderParams
    readonly property alias currentCurveString: editor.curveString
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

    // The Global timing values live on ISettings, not in a profile file, so
    // they emit no overrideChanged. Without this the inheritance breadcrumb and
    // every override-off card's seeded controls keep showing the previous
    // Global until the page is rebuilt — visible on the simple page, where the
    // Global card and the cards inheriting from it share one screen.
    Connections {
        // Route through _reseedFromInherited (cache bump plus, for a card
        // not owning both timing fields, the one chain walk the "Current:"
        // label was going to trigger anyway) rather than a full refreshFromTree, which
        // would re-create the very N-round-trip storm _inheritRev exists to
        // prevent: this fires at slider rate while the Global duration is
        // dragged, and every built card would pay six file opens per tick.
        function onAnimationDurationChanged() {
            root._reseedFromInherited();
        }
        function onAnimationEasingCurveChanged() {
            root._reseedFromInherited();
        }

        target: settingsController.settings
    }
    /// Invalidate the cached inheritance walk, and re-seed the working
    /// controls from it for every timing field this card does not directly
    /// own. Without the re-seed, a card following the Global values keeps the
    /// pre-change ones in currentDuration / currentEasingCurve, and the next
    /// edit the user makes persists those stale values while the italic
    /// "Current:" line beside them reads the new ones.
    ///
    /// Overrides are per field, so "owns an override" is decided per field
    /// too: a card that pins only the duration still has to track Global
    /// curve changes. Only a card owning BOTH fields short-circuits before
    /// the walk — Global cannot reach any of its controls. The overlay of the
    /// card's own stored fields reads `_primaryRaw`, already in hand, so the
    /// cost stays one chain walk with no file open on THIS side. resolvedProfile
    /// itself reads a per-ancestor override file first and consults the
    /// registry only where no user file exists, which is what keeps the walk
    /// honest immediately after a mutation.
    function _reseedFromInherited() {
        root._inheritRev = root._inheritRev + 1;
        if (root._ownsCurveOverride && root._ownsDurationOverride)
            return;
        const r = root._inheritResolved;
        // Own fields win over the fresh inherited values, exactly like the
        // resolver's deeper-wins overlay.
        const effective = Object.assign({}, r, root._primaryRaw);
        root._applyEffective(effective, r.curve);
    }

    readonly property var _inheritResolved: {
        _inheritRev;
        // Coerce to {} when the Q_INVOKABLE returns undefined / null
        // (mid-warmup, malformed path) so downstream `r.curve` /
        // `r.duration` reads don't throw and the bindings fall back
        // cleanly to the curve / duration defaults below.
        return settingsController.animationsPage.resolvedProfile(root.eventPath) || ({});
    }
    // True only for event paths the daemon's overlay service actually
    // consumes as a shader-leg surface. Gates the shader picker, the
    // inline param editor, and the inheritance "Shader: X" banner so
    // a user can't pick a shader on an unsupported path (e.g. the
    // "All Panel Events" parent or `panel.slideIn`) and silently
    // persist a dead override that the daemon resolver would shadow
    // any user-intended setting with via deeper-leaf-wins overlay.
    // Source-of-truth list: `src/core/types/animationshadersupportedpaths.h`.
    // On the card ROOT, not in the editor: the card reaches settingsController
    // and hands the answer down as `shaderLegSupported`, which keeps the editor
    // reusable by hosts with no event path (GlobalTimingDefaultsCard).
    readonly property bool _shaderLegSupported: settingsController.animationsPage.supportsShaderLeg(root.eventPath)
    // Number of shader overrides on paths strictly DEEPER than this card's
    // eventPath. Only meaningful for parent-node cards: a stale leaf
    // override (e.g. `popup.layoutPicker.show = "dissolve"` set in
    // a previous session) silently wins the deeper-leaf-overlay merge in
    // `ShaderProfileTree::resolve` and shadows the parent's value at
    // runtime. Surfaced via the warning banner below with a one-click
    // "Clear shadowing children" button. Refreshed on any
    // shaderProfileChanged signal — see `onShaderProfileChanged` in the
    // `target: settingsController.animationsPage` Connections block below.
    property int _shadowingChildrenCount: 0
    // Bumped on every `shaderEffectsChanged` so any binding that reads
    // a shader-registry Q_INVOKABLE (`availableShaderEffects()`,
    // `shaderParameters()`, etc.) can become reactive to registry
    // mutations by mentioning this revision tick. The Q_INVOKABLE return
    // values are not observed by QML's binding engine — without a
    // tracked dependency on this tick, the bindings would evaluate once
    // and stick at the initial (often empty, mid-warmup) result.
    property int _shaderRegistryRev: 0
    // True when at least one mirror path's STORED state differs from the
    // primary's. The card reads `eventPath` alone, so a mirror that was given
    // its own value in advanced mode is invisible here, and the next edit on
    // this card silently replaces it (every write goes through the group
    // writers). Surfaced by the divergence banner below so the user knows the
    // overwrite is coming before they make it. Always false for the
    // no-mirrors cards, which is every card outside the simple page.
    property bool _mirrorsDiverged: false
    /// How many events hold a value that differs from another: every mirror
    /// whose stored state differs from the primary, plus the primary itself.
    /// Zero when nothing diverges. This counts the events the banner's "set
    /// differently" clause names, which is NOT the same set the next edit
    /// rewrites: both group writers loop _writePaths, so a converging edit
    /// lands on every mirror including the ones already in step. The banner
    /// therefore reports this for the divergence and _writePaths.length for
    /// the reach of the next write. Using one number for both clauses
    /// under-reported the write the moment a card declared two or more
    /// mirrors and only one of them diverged.
    property int _divergentPathCount: 0

    // ── Inheritance summary (italic "Current: …" line when override off) ─
    function inheritSummaryText() {
        var r = root._inheritResolved;
        var curve = r.curve || CurvePresets.defaultEasingCurve;
        var dur = r.duration !== undefined ? r.duration : CurvePresets.defaultDurationMs;
        if (typeof curve === "string" && curve.indexOf("spring:") === 0) {
            const s = CurvePresets.parseSpring(curve);
            return i18n("Spring · ω=%1 · ζ=%2", s.omega.toFixed(1), s.zeta.toFixed(2));
        }
        return i18nc("curve, then duration in milliseconds", "%1 · %2 ms", CurvePresets.curveDisplayName(curve), Math.round(dur));
    }

    function parentChainText() {
        var chain = settingsController.animationsPage.parentChain(root.eventPath);
        // Drop chain[0] (self) — show only ancestors as "window ← global"
        if (chain.length <= 1)
            return "";

        return chain.slice(1).join(" ← ");
    }

    // `effectId` is explicit so callers can snapshot it at user-action
    // time (e.g. when the color dialog opens) rather than reading
    // `root.currentShaderEffectId` at write time. Without that snapshot,
    // a registry refresh that fires while the dialog is open could
    // silently retarget the write at a different effect's param map.
    function _writeShaderParam(effectId, paramId, value) {
        if (!effectId || root._writesRefused)
            return;

        // Bail if the user navigated to a different effect while the
        // dialog (color picker / etc.) was open. The write below carries no
        // effect id, so a late accept can no longer retarget the event at the
        // OLD pack — what it would do instead is land a parameter authored for
        // that pack in the map of the one now showing, where the id means
        // something else or nothing at all. Better to drop the late accept than
        // to write it into state the user explicitly changed.
        if (effectId !== root.currentShaderEffectId)
            return;

        var next = Object.assign({}, root.currentShaderParams || {});
        next[paramId] = value;
        root.currentShaderParams = next;
        // Params-only: never restate the pack. `effectId` above is the RESOLVED
        // id, so on a leaf that inherits its pack, writing it would pin that
        // pack here and sever the cascade. It is still read for the stale-effect
        // guard above, which is the only thing it is good for on this path.
        root._noteWriteResult(root._setShaderParamsOnAll(next));
    }

    /// Batch write — randomize and reset roll N values that should land as one
    /// `setShaderParametersOnPaths` round-trip rather than N of them.
    ///
    /// The `effectId` check here is a non-empty test in practice, NOT the
    /// stale-effect guard `_writeShaderParam` carries. Both call sites pass
    /// `root.currentShaderEffectId` itself and the editor computes and re-emits
    /// the map synchronously in the same handler, so there is no asynchronous
    /// gap for the id to go stale across. It is kept because the empty case is
    /// real (no pack resolved, nothing to write) and because a future payload
    /// that DOES carry a snapshotted id should find the comparison already
    /// here rather than have to reintroduce it.
    function _writeAllShaderParams(effectId, allParams) {
        if (!effectId || effectId !== root.currentShaderEffectId || root._writesRefused)
            return;

        // `currentShaderParams` is deliberately NOT re-staged here. It is an
        // alias onto the editor's own `shaderParams`, and the editor assigns
        // the rolled or default map to that property before emitting, so
        // assigning it again would be writing the value it already holds.
        // Params-only, for the reason _writeShaderParam gives.
        root._noteWriteResult(root._setShaderParamsOnAll(allParams));
    }

    // ── Group writers ───────────────────────────────────────────────
    // The bodies live in AnimationEventCardWriters so this file stays under the
    // size ceiling. These forwarders keep every `root._foo(...)` call site —
    // handlers, bindings and the QML-contract scrape — reading exactly as
    // before. `arguments` forwarding rather than named parameters so a
    // signature change needs no edit here.
    function _setShaderOverrideOnAll() {
        return writers._setShaderOverrideOnAll.apply(writers, arguments);
    }

    function _setShaderParamsOnAll() {
        return writers._setShaderParamsOnAll.apply(writers, arguments);
    }
    function _setOverrideMerged() {
        return writers._setOverrideMerged.apply(writers, arguments);
    }
    function _anyWritePathSupportsShaderLeg() {
        return writers._anyWritePathSupportsShaderLeg.apply(writers, arguments);
    }
    function _clearShaderOverrideOnAll() {
        return writers._clearShaderOverrideOnAll.apply(writers, arguments);
    }
    function _clearOverrideOnAll() {
        return writers._clearOverrideOnAll.apply(writers, arguments);
    }
    function _clearFieldOnAll() {
        return writers._clearFieldOnAll.apply(writers, arguments);
    }
    function _clearShaderOverrideDescendantsOnAll() {
        return writers._clearShaderOverrideDescendantsOnAll.apply(writers, arguments);
    }
    function _allWritePathsHold() {
        return writers._allWritePathsHold.apply(writers, arguments);
    }
    function _refreshMirrorDivergence() {
        return writers._refreshMirrorDivergence.apply(writers, arguments);
    }

    // Never read by name — the forwarders above go through the `writers` id.
    // The property exists to give the object an owner so it lives as long as
    // the card. Deleting it as "unused" takes every group writer with it.
    readonly property AnimationEventCardWriters _writers: AnimationEventCardWriters {
        id: writers

        card: root
    }

    function refreshShaderFromTree() {
        var resolved = settingsController.animationsPage.resolvedShaderProfile(root.eventPath);
        var nextEffectId = (resolved && resolved.effectId) ? resolved.effectId : "";
        // Stale-lock clear on effect switch — same-named ids in
        // different shaders are unrelated.
        if (nextEffectId !== root.currentShaderEffectId)
            root.lockedShaderParams = ({});

        root.currentShaderEffectId = nextEffectId;
        root.currentShaderParams = (resolved && resolved.parameters) ? resolved.parameters : ({});
        // Recompute deeper-override count on every shader-tree update —
        // the warning banner below depends on it. Only meaningful for
        // parent-node cards but we always refresh so the binding stays
        // consistent.
        //
        // Summed across every write path to match
        // _clearShaderOverrideDescendantsOnAll, whose button clears the
        // mirrors' descendants too. No card today is both a parent node and
        // mirrored, so the mirror legs never contribute in the current config.
        // The sum is kept so a future mirrored parent card reports what the
        // button would actually remove.
        //
        // ONE controller call, not one per path. The per-path Q_INVOKABLE is
        // NOT cheap: it rebuilds the whole shader tree every time (a settings
        // read, a JSON parse and a prune walk, because `rawShaderProfile` is
        // not memoised), so looping it here paid that cost once per write path
        // inside a function that runs at drag rate for every visible card.
        // That is the read-in-a-loop shape the controller's own Group-writes
        // block calls out.
        root._shadowingChildrenCount = settingsController.animationsPage.shaderOverrideDescendantCountForPaths(root._writePaths);
        // Divergence is deliberately NOT recomputed here. refreshFromTree owns
        // it, and every call site of this function calls refreshFromTree
        // alongside it (the three shader group writers' finally blocks and
        // onShaderProfileChanged call it immediately after; Component.onCompleted
        // runs refreshFromTree FIRST and this function second, which is
        // equally fine because refreshFromTree recomputes the divergence
        // itself from live reads, so the order between the two is moot).
        //
        // A future caller that runs this ALONE must call refreshFromTree too
        // or the banner goes stale.
    }

    /// Seeds the working controls (timing mode, curve, duration) from an
    /// already-resolved profile. Imperative rather than a binding because the
    /// user edits these directly, so a binding would be severed on first edit
    /// and stop tracking afterwards.
    function _applyEffective(effective, resolvedCurve) {
        var curve = (typeof effective.curve === "string" && effective.curve.length > 0) ? effective.curve : resolvedCurve;
        if (typeof curve === "string" && curve.indexOf("spring:") === 0) {
            // Spring mode is set for a malformed wire string too, since the
            // engine still resolves one. Without it the mode kept its previous
            // value (Easing on first seed) and drew a Duration slider the
            // resolved spring ignores. CurvePresets.parseSpring supplies the
            // values the engine will actually play, so the controls, the
            // thumbnail and the "Current:" line all describe the same spring,
            // and a curve edit commits that spring rather than a fabricated
            // "spring:<stale>,<stale>".
            const s = CurvePresets.parseSpring(curve);
            root.currentTimingMode = CurvePresets.timingModeSpring;
            root.currentSpringOmega = s.omega;
            root.currentSpringZeta = s.zeta;
        } else {
            root.currentTimingMode = CurvePresets.timingModeEasing;
            // With a default fallback, matching the duration line below and
            // inheritSummaryText's read of the same value: without the else,
            // an absent curve (mid-warmup {} resolution, or resolvedProfile's
            // empty-path early return) kept the property's PREVIOUS value —
            // which after a revert is exactly the just-reverted curve, the
            // stale-view class this card's fix exists to close — while the
            // italic "Current:" line already showed the default.
            root.currentEasingCurve = (typeof curve === "string" && curve.length > 0) ? curve : CurvePresets.defaultEasingCurve;
        }
        root.currentDuration = effective.duration !== undefined ? effective.duration : CurvePresets.defaultDurationMs;
    }

    /// @param selfDriven true when the caller is one of this card's own group
    /// writers, refreshing after a write it just made. Only the `_editingTiming`
    /// latch reads it: an EXTERNAL clear must close the timing editor, while our
    /// own per-field revert must not close it under the user mid-edit. Every
    /// other caller (Component.onCompleted, the two signal handlers) leaves it
    /// undefined, which reads as external.
    function refreshFromTree(selfDriven) {
        // A refresh this card did not cause means something else moved the
        // store, which for a refusal-latched card is the discard that was
        // holding it finishing. Retry from here. Own writes pass `selfDriven`,
        // so a drag that trips the latch keeps it for the rest of the drag
        // rather than re-arming on its own refusal refresh.
        if (!selfDriven)
            root._writesRefused = false;

        var raw = settingsController.animationsPage.rawProfile(root.eventPath);
        // Every caller that can MOVE the timing chain bumps _inheritRev before
        // calling — the three timing group writers and onOverrideChanged — so
        // the cached walk is current here and a second C++ chain walk would be
        // redundant. The callers that do not bump cannot move it:
        // Component.onCompleted runs before the binding has ever evaluated, and
        // the shader-side ones (onShaderProfileChanged and the three shader
        // group writers' finally blocks) move the shader tree, which the timing
        // chain does not read. Any NEW caller that can move the chain MUST bump
        // before calling.
        var resolved = root._inheritResolved;
        var hasRaw = raw && Object.keys(raw).length > 0;
        // The card's "Override" toggle reflects ANY direct override at
        // this path — timing curve OR shader assignment. Without the
        // shader half, a user could see an event toggle "off" while a
        // matrix shader was actively firing on every fire of that event
        // (timing override clear, shader override still set), which
        // exactly matches the user-reported "I turned this off but
        // shaders still animate" bug. Reading rawShaderProfile here
        // makes the toggle's checked state honest about both axes.
        // rawShaderProfile returns {} when there's no direct override at
        // this path; any non-empty map (effectId set, parameters set, etc.)
        // indicates a direct override. Mirrors the rawProfile check above.
        // Engaged-empty effectId is the "explicitly disabled" sentinel
        // (writes an inheritance-blocking override at this path; see
        // AnimationsPageController::setShaderOverride). The toggle should
        // read OFF for that state — the user has explicitly turned this
        // event off, not configured it. Only an engaged-NON-empty
        // effectId, or any other shader-profile content (parameters
        // map), counts as "configured ON".
        var rawShader = settingsController.animationsPage.rawShaderProfile(root.eventPath);
        // Boolean-coerce every short-circuit result. The `&&` chain
        // returns the first falsy operand (which can be `null` or
        // `undefined`, NOT `false`), and QML's typed `bool` property
        // setter rejects `undefined` with "Cannot assign [undefined] to
        // bool". Wrap each predicate in Boolean() so the assignment
        // always lands a real true/false.
        var hasShaderEffect = Boolean(rawShader && typeof rawShader.effectId === "string" && rawShader.effectId.length > 0);
        var hasShaderParams = Boolean(rawShader && rawShader.parameters && Object.keys(rawShader.parameters).length > 0);
        var hasShader = hasShaderEffect || hasShaderParams;
        // Stored once; the three ownership tests at the top of this file derive
        // from it. Feeds the editor's ownership caption and the revert
        // affordance's gate.
        root._primaryRawShader = rawShader || ({});
        // The remove control writes the whole group, so its label is answered
        // for the whole group. One controller call rather than a loop: the
        // shader tree is not memoised, so a per-path query would rebuild it
        // once per write path inside a refresh that runs at drag rate.
        root._anyWritePathOwnsShaderPack = settingsController.animationsPage.anyPathOwnsShaderPack(root._writePaths);
        const wasEnabled = root.overrideEnabled;
        root.overrideEnabled = Boolean(hasRaw) || hasShader;
        // A clear that did not come through the toggle's OFF arm — a page
        // Discard, a profile switch, another surface writing this path — drops
        // overrideEnabled without touching the latch, leaving the toggle
        // reading ON and the timing section open over a card that stores
        // nothing.
        //
        // Gated on a true→false TRANSITION, not on the current value. Both
        // signal handlers refresh far more often than this card changes: the
        // shader signal is a path-agnostic broadcast every card accepts, and
        // onOverrideChanged accepts any ancestor path. Testing
        // `!overrideEnabled` alone therefore closed the timing editor of a card
        // the user had just latched open whenever ANY other card on the page
        // was edited.
        //
        // Own writes are excluded via `selfDriven` on top of that: a per-field
        // revert that happens to clear the last field must not close the editor
        // under the user mid-edit. The `_committing` latches cannot stand in
        // for it — every writer clears them before it refreshes, so they are
        // always false by the time this runs.
        if (wasEnabled && !root.overrideEnabled && !selfDriven)
            root._editingTiming = false;
        // Effective values feed the controls. With no direct override the
        // controls preview the resolved profile from the parent chain.
        // Overrides are per field, so a direct override decides only the
        // fields it actually holds and the resolved chain fills the rest —
        // a curve-only override (a duration revert, or a curve edit on a
        // path with no prior override) must NOT reset the Duration slider
        // to the library default while the caption beside it says the
        // duration is inherited.
        var effective = hasRaw ? Object.assign({}, resolved, raw) : resolved;
        // The resolved curve still travels as the explicit fallback: the
        // merge above fills a missing raw curve from `resolved`, but a raw
        // curve that is present-yet-empty would survive the merge, and
        // _applyEffective's non-empty check then needs somewhere to fall.
        root._applyEffective(effective, resolved.curve);
        // The stored profile this card's own captions read, taken from the
        // `raw` read at the top of this function rather than repeated.
        root._primaryRaw = raw || ({});
        root._refreshMirrorDivergence();
    }

    // Per-axis commits — each writes ONLY the field the user edited, so the
    // other field keeps inheriting. currentDuration / currentCurveString are
    // seeded from the RESOLVED (inherited) profile whenever this card owns no
    // override on that field, so committing the untouched axis would pin a
    // copy of the Global value here as a direct override: this event would
    // then silently stop tracking later Global changes on a field the user
    // never edited. That was exactly the old commitOverride's bug in advanced
    // mode (a Duration drag pinned the curve and vice versa); simple mode had
    // already carved the curve out, and the per-axis split extends the same
    // rule to both fields in both modes. The controller stamps the `name`
    // field automatically.
    function commitDurationOverride() {
        if (root._writesRefused)
            return;

        // The merged writer overlays only the fields in `profile`, and the
        // `undefined` curve means "each path keeps its own curve, or keeps
        // inheriting" — decided PER PATH so a mirror that owns a curve is
        // preserved and one that inherits stays inheriting.
        root._noteWriteResult(root._setOverrideMerged({
            "duration": root.currentDuration
        }, undefined));
    }

    function commitCurveOverride() {
        if (root._writesRefused)
            return;

        // Empty profile map: nothing but the curve travels. Each path's own
        // stored duration (or its absence) survives the merge untouched.
        root._noteWriteResult(root._setOverrideMerged({}, root.currentCurveString));
    }

    // Two emitters pass an empty path, and both mean "reload everything":
    // `shaderProfileChanged` on a full-tree reload, and `overrideChanged` from
    // the controller's `forgetCachedOverrideFiles`, which fires when somebody
    // OUTSIDE the settings app writes to the profiles directory. The controller
    // suppresses that broadcast for its own writes, which carry precise
    // per-path signals instead.
    function _pathAffectsThisCard(path) {
        if (path === "")
            return true;

        // Mirrors are included even though the card never READS them: the
        // divergence banner's state depends on their stored values, so an
        // edit landing on a mirror alone (the same event's own card on the
        // advanced page) has to re-run this card's refresh or the banner goes
        // stale. Refreshing costs a re-read of the unchanged primary.
        // "global" is the tree ROOT, and ProfilePaths::parentPath maps every
        // category root to it as a bare literal, so a startsWith(path + ".")
        // test never matches it. Every other ancestor IS spelled as a dotted
        // prefix, so the loop below covers them; the root is the only gap.
        // The case this closes is a `global.json` override FILE (a motion-set
        // import writes one, and a Discard / Reset / scoped revert emits
        // overrideChanged("global") for it). Edits to the Global CARD are a
        // different path — that writes ISettings, handled by the Connections
        // block above.
        if (path === "global")
            return true;

        const paths = root._writePaths;
        for (var i = 0; i < paths.length; ++i) {
            if (path === paths[i] || paths[i].startsWith(path + "."))
                return true;
        }
        return false;
    }

    implicitHeight: card.implicitHeight
    Layout.fillWidth: true
    Component.onCompleted: {
        // Name whatever _validMirrorPaths dropped. The drop itself is the
        // guard — it keeps a rejected mirror out of every writer and out of the
        // divergence check, so the banner cannot latch on a path no edit could
        // ever converge. This turns the remaining silence into a one-line
        // console message naming the dropped path.
        //
        // Derived from the SET DIFFERENCE against _validMirrorPaths rather than
        // by re-running isValidEventPath here. Re-deciding would make this a
        // second, independent copy of the keep rule, so any future change to
        // what _validMirrorPaths keeps would leave the warning describing a
        // drop that did not happen (or staying silent about one that did).
        const declared = root.mirrorPaths || [];
        const kept = root._validMirrorPaths;
        for (var i = 0; i < declared.length; ++i) {
            if (kept.indexOf(declared[i]) === -1)
                console.warn("AnimationEventCard(" + root.eventPath + "): mirrorPaths entry '" + declared[i] + "' was not accepted as an event path; it has been dropped and will receive no writes");
        }
        refreshFromTree();
        refreshShaderFromTree();
    }

    Connections {
        function onOverrideChanged(path) {
            // _pathAffectsThisCard treats path === "" as "tree fully
            // reloaded" broadcast and returns true unconditionally, so
            // a single check covers both per-path filtering and the
            // global-broadcast carve-out.
            if (root._committing || !root._pathAffectsThisCard(path))
                return;

            // The signal is per-path but the resolved profile depends on
            // the entire ancestor chain, so any change at-or-above this
            // path can shift the inheritance banner. Bump the revision
            // tick FIRST so _inheritResolved is already current when
            // refreshFromTree reads it.
            root._inheritRev++;
            root.refreshFromTree();
        }

        function onShaderProfileChanged(path) {
            // Empty-string path is the controller's "tree fully reloaded"
            // broadcast — set/clear/clearDescendants all route through
            // `Settings::setShaderProfileTree` which the controller
            // relays as a single path-agnostic emit (see
            // `animationspagecontroller.cpp` — `Q_EMIT
            // shaderProfileChanged(QString())`). _pathAffectsThisCard
            // treats "" as the broadcast sentinel and returns true so
            // every card refreshes for it; per-path emits still get
            // the prefix filter.
            if (root._committingShader || !root._pathAffectsThisCard(path))
                return;

            root.refreshShaderFromTree();
            // The card's "Override" toggle now reflects whether either
            // a timing OR a shader override exists at this path, so a
            // shader-only change has to re-flip refreshFromTree's
            // overrideEnabled binding too. Without this, clearing the
            // shader on a path with no timing override would leave the
            // toggle visually "on" until something else triggered a
            // refreshFromTree call.
            root.refreshFromTree();
        }

        function onShaderEffectsChanged() {
            // One tick invalidates every Q_INVOKABLE-derived shader binding
            // on this card (picker's `_effects`, param editor's `_paramSchema`).
            root._shaderRegistryRev++;
        }

        // The refusal latch's guaranteed release, and it has to be THIS signal.
        //
        // The latch is what stops a drag re-issuing a write the controller is
        // refusing, and only a refresh the card did not drive itself clears it.
        // The discard that causes those refusals does NOT end with a
        // path-agnostic broadcast: its terminal handler emits `overrideChanged`
        // only for the profile files the worker actually restored, so a card
        // none of those paths reaches never refreshes, and the latch would stay
        // set for the rest of the session. Nothing reconstructs the card to
        // recover, because the list's Loaders latch built and never unload.
        //
        // `pendingChangesChanged` is emitted unconditionally by that same
        // handler, so it is the one signal every card is guaranteed to see when
        // the discard settles. It also fires on ordinary writes, which is
        // harmless: a card still mid-drag re-arms the latch on its very next
        // refused tick, so this costs at most one extra toast per discard and
        // keeps the anti-repeat property that matters.
        function onPendingChangesChanged() {
            root._writesRefused = false;
        }

        target: settingsController.animationsPage
    }

    SettingsCard {
        // ── Shared timing + shader editor body ────────────────────
        // All the inline timing controls (curve thumbnail,
        // Customize… button, timing-mode combo, duration slider)
        // and shader controls (picker + parameter editor + color
        // dialog + curve dialog) used to live here. They've been
        // hoisted into the reusable `AnimationProfileEditor` so
        // this card and `GlobalTimingDefaultsCard` share one
        // implementation. See `AnimationProfileEditor.qml`.
        // The editor's working-state properties (timingMode,
        // duration, easingCurve, springOmega, springZeta,
        // shaderEffectId, shaderParams, lockedShaderParams) are
        // exposed back through this card via property aliases at
        // the top of the file, so `refreshFromTree`, the per-axis
        // commits, `_writeShaderParam`, and the controller signal
        // handlers continue to read and write through the same
        // names as before.

        id: card

        anchors.fill: parent
        headerText: root.eventLabel
        showToggle: true
        // Checked = any direct override exists, OR the user has opened the
        // timing editor this session without pinning anything yet (the
        // _editingTiming latch). The latch half keeps the toggle honest about
        // what flipping it ON now does: it opens the editor and writes
        // nothing.
        toggleChecked: root._timingEditorOpen
        // The toggle REPORTS whether this event has any direct override; it is
        // not a precondition for making one. Gating the body on it would
        // disable and hide every row including the shader picker, so a user
        // could not drop a shader on an event without first creating a timing
        // override they did not want. Picking a shader flips the toggle on by
        // itself, because refreshFromTree derives it from both axes — and it
        // opens the timing section with it, because `showTimingSection` is fed
        // from `_timingEditorOpen`, which is that same two-axis reading. That
        // is not an oversight to be fixed by giving the section its own
        // spelling: `_timingEditorOpen` deliberately drives the toggle, the
        // section's visibility and both edit guards from ONE expression, and
        // the fifth site that remembers only half of it is the bug that
        // spelling prevents. An event with a shader and no timing override
        // therefore shows the timing rows, with both captions correctly
        // reporting that it is following inherited values.
        gateBodyOnToggle: false
        collapsible: root.collapsible
        onToggleClicked: function (checked) {
            if (checked) {
                // Open the timing editor, write nothing. The old behaviour
                // (committing a snapshot of the inherited values so the
                // derived toggle state would stick) is what silently pinned a
                // copy of the Global curve and duration here the moment the
                // toggle was flipped — an override the user never asked for.
                // An override is now created only by editing a control, and
                // only for the edited field.
                root._editingTiming = true;
            } else {
                // The toggle reports whether this event has ANY direct
                // override (refreshFromTree: hasRaw || hasShader), so turning
                // it off must REMOVE both, returning the event to inheritance.
                //
                // It used to write the engaged-empty shader sentinel instead.
                // That sentinel is an explicit "None" that BLOCKS inheritance,
                // which is a different state from having no override — so the
                // card then rendered "Inheriting from: <parent>" while the
                // stored tree actively refused to inherit, and the block
                // survived toggling back on because the toggle's ON path never
                // writes a shader. Blocking an inherited shader is the picker's job and
                // the picker is reachable independently of this toggle.
                //
                // Shader first, then timing: if the timing clear fails
                // mid-flight (a QFile error inside clearOverride's on-disk
                // write), the shader side is already committed, so a partial
                // failure still moves toward the user's intent instead of
                // recording neither half.
                if (root._anyWritePathSupportsShaderLeg())
                    root._clearShaderOverrideOnAll();

                // Gated: the controller refuses (and toasts) during an async
                // discard, and on a partial failure — closing the editor anyway
                // left the toggle visibly off beside a message saying it could
                // not be changed. refreshFromTree has already run inside the
                // call, so the toggle re-derives from the tree either way.
                if (root._clearOverrideOnAll())
                    root._editingTiming = false;
            }
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            // ── Inheritance banners + "Current:" line ─────────────────
            // Presentation-only stack, split into its own component to
            // keep this card within the project file-size ceiling. Every
            // value is fed from the card's derived state; the shadowing
            // warning's one action is emitted back.
            AnimationEventCardBanners {
                Layout.fillWidth: true
                isParentNode: root.isParentNode
                overrideActive: root.overrideEnabled
                timingEditorOpen: root._timingEditorOpen
                shadowingChildrenCount: root._shadowingChildrenCount
                mirrorsDiverged: root._mirrorsDiverged
                divergentPathCount: root._divergentPathCount
                writePathCount: root._writePaths.length
                parentChain: root.parentChainText()
                inheritSummary: root.inheritSummaryText()
                // The return is deliberately not read. Both branches are
                // self-correcting: the writer's `finally` runs
                // refreshShaderFromTree() + refreshFromTree() whether the clear
                // landed or was refused, so the shadowing count and the banner
                // are current either way, and the controller owns the refusal
                // toast. Reading the -1 here would only duplicate that.
                onClearShadowingRequested: root._clearShaderOverrideDescendantsOnAll()
            }

            // Section visibility splits the per-axis behaviour the
            // inline layout used to encode: the timing section only
            // surfaces while the timing editor is engaged (a direct
            // override exists, or the toggle latched it open), while
            // the shader section is visible whenever the event
            // supports a shader leg (independent of the timing
            // override — picking a shader doesn't require enabling
            // the timing override).
            AnimationProfileEditor {
                id: editor

                Layout.fillWidth: true
                eventLabel: root.eventLabel
                shaderLegSupported: root._shaderLegSupported
                showTimingSection: root._timingEditorOpen
                simpleTiming: root.simpleTiming
                showOverrideStatus: true
                curveOverridden: root._ownsCurveOverride
                // The caption describes THIS event, the remove control acts on
                // the whole write group, so the two are fed from different
                // predicates on purpose. See `_anyWritePathOwnsShaderPack`.
                shaderOwnsPack: root._ownsShaderPack
                shaderOwnsParamsOnly: root._ownsShaderParamsOnly
                shaderOverrideStored: root._storesShaderOverride
                shaderBlocksInherited: root._shaderBlocksInherited
                shaderPackRemovable: root._anyWritePathOwnsShaderPack
                durationOverridden: root._ownsDurationOverride
                enableLocking: true
                enableRandomize: true
                enableImage: false
                // Live per-field commit — the slider's 30 Hz drag fires
                // `durationEdited` on every move, writing only the duration
                // field of the merged Profile JSON; curve edits (mode combo,
                // curve dialog, spring dialog) write only the curve field.
                // The guard mirrors the section's own visibility so a
                // programmatic emit can never write while the editor is
                // hidden.
                onDurationEdited: {
                    if (root._timingEditorOpen)
                        root.commitDurationOverride();
                }
                onCurveEdited: {
                    if (root._timingEditorOpen)
                        root.commitCurveOverride();
                }
                // The per-field revert links restore inheritance for one
                // field without touching the other or the shader leg.
                //
                // The latch is set on SUCCESS so the timing editor stays open
                // afterwards. `selfDriven` alone is not enough: it only helps a
                // card whose latch is already set, and the common case is an
                // override that predates the session, where the section is open
                // via `overrideEnabled` and the latch is false. Reverting the
                // last remaining field then drops overrideEnabled and collapses
                // the editor under the cursor of the user who just clicked
                // inside it. Gated on the return, which is false ONLY when the
                // controller refused the call outright and attempted nothing (an
                // async discard owns the tree, and it toasts). A PARTIAL failure
                // reports the count that did land and so still latches the editor
                // open, because the primary path really was cleared and
                // collapsing the card under the user is the regression this whole
                // interaction exists to prevent.
                onCurveRevertRequested: {
                    if (root._clearFieldOnAll("curve"))
                        root._editingTiming = true;
                }
                onDurationRevertRequested: {
                    if (root._clearFieldOnAll("duration"))
                        root._editingTiming = true;
                }
                // Picker model fed via the registry-tick dependency
                // so the binding re-evaluates on
                // `shaderEffectsChanged`.
                // Path-aware list: pre-filtered to the shaders that can drive
                // this event, so the category picker only offers compatible
                // shaders (e.g. the geometry-only window-morph is omitted on a
                // show/hide event). `dimmed`/`dimReason` remain on each row as
                // always-false QML-compat fields.
                availableShaders: {
                    void (root._shaderRegistryRev);
                    return settingsController.animationsPage.availableShaderEffectsForPath(root.eventPath);
                }
                // Parameter schema for the picked shader, fed in the same
                // registry-tick-bound way so the editor doesn't reach the
                // settingsController context itself.
                shaderParamSchema: {
                    void (root._shaderRegistryRev);
                    return editor.shaderEffectId.length > 0 ? settingsController.animationsPage.shaderParameters(editor.shaderEffectId) : [];
                }
                // Lock state is deliberately not handled here: it is working
                // state only, and AnimationProfileEditor does not re-emit it
                // (see its ShaderParamsEditor handler). Nothing to connect.
                onShaderEffectActivated: function (id) {
                    var sid = id || "";
                    // The no-op guards below test EVERY write path, not
                    // just the primary: with mirrorPaths set, the primary
                    // can already hold the picked value while a mirror
                    // still carries a divergent one, and returning early
                    // on the primary alone would leave the group split.
                    if (root._allWritePathsHold(sid))
                        return;

                    // "None" picks the engaged-empty inheritance-blocking
                    // sentinel. Otherwise this is one of two different things
                    // wearing one signal, and they must not write the same
                    // parameter map.
                    //
                    // A SWITCH to a different pack drops the previous pack's
                    // parameters, because parameter ids are per-pack and the
                    // old map means nothing to the new one. That is the
                    // long-standing behaviour and it is correct.
                    //
                    // A PROMOTION — picking the pack this event is already
                    // showing, to own it rather than inherit it — must keep
                    // them. Passing an empty map here silently deleted the
                    // parameters the user had just tuned, because the guard
                    // above cannot short-circuit a promotion: after a
                    // params-only write the event holds no `effectId` at all,
                    // and `allPathsHoldShaderEffect` treats an absent id as
                    // equal to nothing, sentinel included. So the write went
                    // ahead and `setShaderOverrideOnPaths` rebuilt the profile
                    // from the id alone.
                    const promoting = sid.length > 0 && sid === root.currentShaderEffectId;
                    root._setShaderOverrideOnAll(sid, promoting ? (root.currentShaderParams || ({})) : ({}));
                }
                // Drop this event's own shader choice so it follows its
                // ancestors again. The opposite of the None sentinel above,
                // which BLOCKS them.
                onShaderRevertRequested: {
                    root._clearShaderOverrideOnAll();
                }
                onShaderParamWriteRequested: function (effectId, paramId, value) {
                    root._writeShaderParam(effectId, paramId, value);
                }
                // Lock-toggle handlers are no-ops here —
                // AnimationProfileEditor self-updates its own
                // `lockedShaderParams` (which is aliased onto this
                // card's `lockedShaderParams`) before emitting, so a
                // re-assign here would be idempotent. The signals
                // remain connect-free until / unless the lock state
                // becomes persistent (today it's working-state only).
                onRandomizeRequested: function (rolled) {
                    // Editor already staged `rolled` onto its
                    // `shaderParams`; this card's persistence is
                    // through the controller, so route the rolled map
                    // through the batch writer (a single
                    // setShaderParametersOnPaths call carrying every roll).
                    root._writeAllShaderParams(root.currentShaderEffectId, rolled);
                }
                onResetRequested: function (defaults) {
                    // Same batch path as randomize — one
                    // setShaderParametersOnPaths carrying every param at its
                    // default.
                    root._writeAllShaderParams(root.currentShaderEffectId, defaults);
                }
            }
        }
    }
}
