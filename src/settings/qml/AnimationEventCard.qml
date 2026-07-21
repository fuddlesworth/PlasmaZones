// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable card for per-event animation configuration.
 *
 * Each card edits one event in the `PhosphorAnimation::ProfilePaths`
 * taxonomy (e.g. `editor.snapIn`, `osd.show`). The override toggle
 * creates/clears one Profile JSON file under
 * `~/.local/share/plasmazones/profiles/`; the daemon's existing
 * `ProfileLoader` watches that dir and live-reloads the registry.
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
 * Internal state (`overrideEnabled`, the `current*` and `locked*` aliases) is
 * public only so the aliases resolve, and must not be assigned by consumers:
 * the card seeds it from the profile tree and commits it back, so an outside
 * write is either overwritten on the next refresh or persisted as a user edit.
 *
 *   - simpleTiming: bool — hides the timing-mode combo and the curve editor,
 *     and keeps a duration-only edit from pinning the inherited curve
 *   - mirrorPaths: list<string> — extra event paths every write is echoed to,
 *     so one card can front several events (open mirrored onto close)
 */
Item {
    // Both the curve editor and the colour picker now live inside the
    // shared `AnimationProfileEditor`. Removing them from this file
    // keeps the dialog ownership in one place — when the editor is
    // hidden (override toggle off + shader leg unsupported) so are
    // its dialogs. The card no longer hosts any dialogs of its own.

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
    /// observing profile-change signals: the shader signal is a
    /// path-agnostic broadcast, so an observer cannot tell a user edit on
    /// this card from an unrelated card's edit, a Discard, or a profile
    /// switch, and would clobber divergent mirror values (or re-dirty the
    /// config immediately after a Discard). Writing through the same call
    /// the user's action triggers has none of those failure modes.
    /// Deliberately `var` (a JS array), not `list<string>`: _writePaths does
    /// `[eventPath].concat(mirrorPaths)`, and Array.prototype.concat SPREADS
    /// only true JS arrays. A QML list proxy would be appended as one element,
    /// silently turning every mirror write into a write to a bogus path.
    property var mirrorPaths: []

    /// Raw stored profile per write path, refreshed by refreshFromTree.
    /// _setOverrideMerged reads it to merge over each path's own stored
    /// fields. It exists to avoid DISK I/O: the writer runs from the duration
    /// slider's valueChanged, i.e. every tick of a drag, and reading each
    /// path's stored profile there meant one synchronous file open per path
    /// per tick. Re-entrancy is _committing's job, not this cache's.
    property var _pathProfiles: ({})

    /// True while _setOverrideMerged is writing, so the overrideChanged each
    /// write emits does not re-enter refreshFromTree mid-loop.
    property bool _committing: false

    /// The shader-axis counterpart, for _setShaderOverrideOnAll.
    property bool _committingShader: false

    /// Every path this card writes: its own, then the mirrors.
    readonly property var _writePaths: [root.eventPath].concat(root.mirrorPaths)
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
    // ── Editor-owned working state (proxied via aliases) ────────────
    // The shared `AnimationProfileEditor` (declared inside the card's
    // body below) owns the timing + shader working state and the
    // dialogs / widgets that drive them. This card's existing logic
    // (`refreshFromTree`, `commitOverride`, `_writeShaderParam`, ...)
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
        // Bump the inheritance cache only. A full refreshFromTree here would
        // re-create the very N-round-trip storm _inheritRev exists to prevent:
        // dragging the Global duration fires this at slider rate, and every
        // built card would pay six file opens plus two chain walks per tick.
        // The breadcrumb and any override-OFF card read through
        // _inheritResolved, which the bump alone invalidates. A card that owns
        // an override shows its own values and does not care.
        function onAnimationDurationChanged() {
            root._reseedFromInherited();
        }
        function onAnimationEasingCurveChanged() {
            root._reseedFromInherited();
        }

        target: settingsController.settings
    }
    /// Invalidate the cached inheritance walk, and re-seed the working
    /// controls from it when this card owns no override. Without the re-seed,
    /// an override-OFF card keeps the pre-change Global values in
    /// currentDuration / currentEasingCurve, and the moment the user flips
    /// Override on, commitOverride persists those stale values while the
    /// italic "Current:" line beside them reads the new ones.
    ///
    /// Costs one chain walk and no ADDITIONAL walk beyond the one the "Current:"
    /// label already triggers, so the N-round-trip storm the bump-only handler
    /// was guarding against stays prevented: a card that owns an override
    /// short-circuits before the walk. The walk is registry-served in the app,
    /// though resolvedProfile does fall back to a per-ancestor file read when a
    /// level has no registry entry.
    function _reseedFromInherited() {
        root._inheritRev = root._inheritRev + 1;
        if (root.overrideEnabled)
            return;
        const r = root._inheritResolved;
        root._applyEffective(r, r.curve);
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
    // Source-of-truth list: `src/core/animationshadersupportedpaths.h`.
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
        if (!effectId)
            return;

        // Bail if the user navigated to a different effect while the
        // dialog (color picker / etc.) was open. Calling
        // `setShaderOverride` with the stale effect id would silently
        // reassign the eventPath to the OLD effect, undoing the user's
        // navigation and reviving a dropped param map. Better to drop the
        // late accept than to clobber state the user explicitly changed.
        if (effectId !== root.currentShaderEffectId)
            return;

        var next = Object.assign({}, root.currentShaderParams || {});
        next[paramId] = value;
        root.currentShaderParams = next;
        root._setShaderOverrideOnAll(effectId, next);
    }

    // ── Group writers ───────────────────────────────────────────────
    // Every controller write this card performs goes through one of
    // these so `mirrorPaths` cannot be silently bypassed by a future
    // call site.
    /// Suppressed the same way the timing writer is: each setShaderOverride
    /// relays a path-agnostic shaderProfileChanged(QString()) broadcast, so an
    /// N-path write costs N full refreshes here (and every OTHER card on the
    /// page refreshes N times too — that part is inherent to the broadcast and
    /// out of this card's hands). Reached from the param sliders, so this runs
    /// at drag rate.
    function _setShaderOverrideOnAll(effectId, params) {
        root._committingShader = true;
        try {
            const paths = root._writePaths;
            for (var i = 0; i < paths.length; ++i)
                settingsController.animationsPage.setShaderOverride(paths[i], effectId, params);
        } finally {
            // Same try/finally reasoning as _setOverrideMerged: QML has no
            // RAII, and a latched flag here would stop the card tracking
            // external shader edits for the rest of the session.
            root._committingShader = false;
            root.refreshShaderFromTree();
            root.refreshFromTree();
        }
    }

    /// Writes `profile` to every path this card controls, merged over each
    /// path's OWN stored profile so fields this card does not edit
    /// (minDistance, sequenceMode, staggerInterval, presetName) survive
    /// instead of being truncated. A motion set can write those to a leaf
    /// (see motionsetdomain.cpp), and a card that overwrote the whole map
    /// would silently drop them the moment the user nudged Duration.
    ///
    /// `curveFromCommit` is the curve the user can actually see and edit, so
    /// it travels to every path. Pass `undefined` when the card shows no
    /// curve control: each path then keeps its OWN curve, so a path that owns
    /// one has it preserved and a path that inherits stays inheriting. The
    /// card must not decide a curve on the user's behalf.
    function _setOverrideMerged(profile, curveFromCommit) {
        // Suppress the per-write refresh: setOverride emits overrideChanged
        // synchronously, so without this an N-path card pays N refreshes per
        // tick of a duration drag, each re-reading every path. One refresh
        // after the loop sees the same end state. For the common N=1 card this
        // is a wash (one emit, one refresh, either way) — the saving is real
        // only for a mirrored card, which is why the flag's REAL job is the
        // consistency of the loop rather than the saving.
        // try/finally, not a straight-line set/clear pair: QML has no RAII, and
        // anything that throws inside the loop (a Q_INVOKABLE argument
        // conversion, or settingsController resolving undefined during a page
        // teardown) would otherwise leave this latched TRUE. That is not
        // transient — onOverrideChanged would early-return for the rest of the
        // session, so the card would stop tracking Discard, profile switches
        // and external edits while still writing on every slider tick, and the
        // list's Loaders latch built and never unload, so it is never
        // reconstructed to recover.
        root._committing = true;
        try {
            root._setOverrideMergedLoop(profile, curveFromCommit);
        } finally {
            // Both the flag AND the refresh are in the finally. Every
            // overrideChanged the loop emitted was deliberately swallowed on
            // the promise that one refresh follows it, so a throw that skipped
            // the refresh would break exactly the invariant the flag exists to
            // defend: the writes that DID land would never reach the card, and
            // _pathProfiles / overrideEnabled / the divergence banner would
            // stay stale until some unrelated signal arrived.
            root._committing = false;
            root._inheritRev++;
            root.refreshFromTree();
        }
    }

    /// The write loop itself. Split out so _setOverrideMerged's try/finally
    /// reads as one statement and the flag's lifetime is obvious.
    function _setOverrideMergedLoop(profile, curveFromCommit) {
        const paths = root._writePaths;
        for (var i = 0; i < paths.length; ++i) {
            var raw = root._pathProfiles[paths[i]] || ({});
            var perPath = Object.assign({}, raw);
            Object.assign(perPath, profile);
            if (curveFromCommit !== undefined)
                perPath.curve = curveFromCommit;
            else if (typeof raw.curve === "string" && raw.curve.length > 0)
                perPath.curve = raw.curve;
            else
                delete perPath.curve;
            settingsController.animationsPage.setOverride(paths[i], perPath);
        }
    }

    /// Suppressed like the write path: each clearOverride emits
    /// overrideChanged synchronously, so an unguarded loop pays one full
    /// refresh per path (each re-reading EVERY path) and recomputes the
    /// divergence banner against a half-cleared group, flickering it on
    /// mid-loop.
    function _clearOverrideOnAll() {
        root._committing = true;
        try {
            const paths = root._writePaths;
            for (var i = 0; i < paths.length; ++i)
                settingsController.animationsPage.clearOverride(paths[i]);
        } finally {
            root._committing = false;
            root._inheritRev++;
            root.refreshFromTree();
        }
    }

    // Returns the number cleared, or -1 if ANY path refused (the
    // controller's "async discard in flight" sentinel). Summing a -1 into
    // the count would make a refusal indistinguishable from a smaller
    // successful clear.
    function _clearShaderOverrideDescendantsOnAll() {
        const paths = root._writePaths;
        var cleared = 0;
        var refused = false;
        for (var i = 0; i < paths.length; ++i) {
            const n = settingsController.animationsPage.clearShaderOverrideDescendants(paths[i]);
            if (n < 0)
                refused = true;
            else
                cleared += n;
        }
        return refused ? -1 : cleared;
    }

    /// True iff every write path already carries @p effectId as its DIRECT
    /// shader override (the empty string being the engaged-empty sentinel,
    /// which is distinct from "no override at all").
    function _allWritePathsHold(effectId) {
        const paths = root._writePaths;
        for (var i = 0; i < paths.length; ++i) {
            const raw = settingsController.animationsPage.rawShaderProfile(paths[i]);
            const direct = (raw && typeof raw.effectId === "string") ? raw.effectId : undefined;
            if (direct !== effectId)
                return false;
        }
        return true;
    }

    /// Canonical form of one path's stored state: its direct timing profile
    /// and its direct shader profile, both coerced to {} when absent so a
    /// missing override and an empty one compare equal. Both come back as
    /// QVariantMaps, whose JS key order is the map's own sorted order, so two
    /// paths holding the same values always stringify identically.
    /// Prefers the _pathProfiles snapshot when it holds this path: it is
    /// refreshed on the same pass that calls this, and rawProfile() is a
    /// synchronous file open, so re-reading here defeated the cache's whole
    /// purpose on every drag tick.
    function _storedStateKey(path) {
        const cached = root._pathProfiles[path];
        const profile = cached || settingsController.animationsPage.rawProfile(path) || ({});
        const shader = settingsController.animationsPage.rawShaderProfile(path) || ({});
        // Divergence is measured on exactly what this card WRITES to every
        // path, which is duration and the shader leg. Everything else a leaf
        // can carry is per-path by design: the curve, because simple mode
        // writes per-path curves so a path that owns one keeps it, and the
        // motion-set fields (minDistance, sequenceMode, staggerInterval,
        // presetName), because the merged writer preserves each path's own.
        // Counting any of those latched the banner ON permanently with no
        // control able to clear it, while the banner promised the next edit
        // would converge the group. An allowlist rather than an exclusion
        // list, so a field added to the stored profile later cannot latch it
        // again without also being added to what the card writes.
        const compared = {};
        if (profile.duration !== undefined)
            compared.duration = profile.duration;
        // The curve exclusion above is conditional on the SIMPLE-mode leg,
        // which writes per-path curves. The advanced leg passes
        // currentCurveString to _setOverrideMerged, so it writes ONE curve to
        // every path — a divergent mirror curve there IS clobbered by the next
        // edit, and the banner has to say so.
        if (!root.simpleTiming && profile.curve !== undefined)
            compared.curve = profile.curve;
        return JSON.stringify([compared, shader]);
    }

    /// Recompute _mirrorsDiverged. Called from both refreshers so it tracks
    /// every signal that can move either tree.
    function _refreshMirrorDivergence() {
        const mirrors = root.mirrorPaths;
        if (!mirrors || mirrors.length === 0) {
            root._mirrorsDiverged = false;
            return;
        }
        const primary = root._storedStateKey(root.eventPath);
        for (var i = 0; i < mirrors.length; ++i) {
            if (root._storedStateKey(mirrors[i]) !== primary) {
                root._mirrorsDiverged = true;
                return;
            }
        }
        root._mirrorsDiverged = false;
    }

    /// Batch write — randomize rolls N values that should land as one
    /// `setShaderOverride` round-trip. Same stale-effect guard as
    /// `_writeShaderParam`.
    function _writeAllShaderParams(effectId, allParams) {
        if (!effectId || effectId !== root.currentShaderEffectId)
            return;

        root.currentShaderParams = allParams;
        root._setShaderOverrideOnAll(effectId, allParams);
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
        // the warning banner below depends on it. Cheap (O(N) over
        // overriddenPaths). Only meaningful for parent-node cards but
        // we always refresh so the binding stays consistent.
        //
        // Summed across every write path to match
        // _clearShaderOverrideDescendantsOnAll, whose button clears the
        // mirrors' descendants too. No card today is both a parent node and
        // mirrored, so the mirror legs of this loop never run in the current
        // config. The sum is kept so a future mirrored parent card reports
        // what the button would actually remove.
        const countPaths = root._writePaths;
        var shadowing = 0;
        for (var i = 0; i < countPaths.length; ++i)
            shadowing += settingsController.animationsPage.shaderOverrideDescendantCount(countPaths[i]);
        root._shadowingChildrenCount = shadowing;
        root._refreshMirrorDivergence();
    }

    /// Seeds the working controls (timing mode, curve, duration) from an
    /// already-resolved profile. Imperative rather than a binding because the
    /// user edits these directly, so a binding would be severed on first edit
    /// and stop tracking afterwards.
    function _applyEffective(effective, resolvedCurve) {
        var curve = (typeof effective.curve === "string" && effective.curve.length > 0) ? effective.curve : resolvedCurve;
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

    function refreshFromTree() {
        var raw = settingsController.animationsPage.rawProfile(root.eventPath);
        // Every caller that can MOVE the timing chain bumps _inheritRev first
        // (_setOverrideMerged, onOverrideChanged), so the cached walk is
        // current here and a second C++ chain walk would be redundant. The two
        // that do not bump cannot move it: Component.onCompleted runs before
        // the binding has ever evaluated, and both shader-side callers
        // (onShaderProfileChanged and _setShaderOverrideOnAll's finally) move
        // the shader tree, which the timing chain does not read. A fifth caller
        // that can move the chain MUST bump before calling.
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
        root.overrideEnabled = Boolean(hasRaw) || hasShader;
        // Effective values feed the controls. When override is off the
        // controls preview "what would happen if you turned it on" =
        // the resolved profile from the parent chain. When on, the
        // raw fields decide.
        var effective = hasRaw ? raw : resolved;
        // Timing MODE follows the curve that actually applies, which is not
        // always the one on this card's own override. A simple-mode edit
        // commits duration WITHOUT a curve (see commitOverride) precisely so
        // the curve keeps inheriting, so `effective.curve` is absent while an
        // inherited spring still governs. Reading only `effective` would force
        // Easing and draw a Duration slider that a resolved spring ignores,
        // while suppressing the hint that explains why. Duration still comes
        // from `effective` below — only the mode falls back.
        root._applyEffective(effective, resolved.curve);
        // Cache each write path's stored profile for _setOverrideMerged. This
        // runs on every overrideChanged, which is exactly when a path's stored
        // state can have moved.
        var cache = {};
        for (var pi = 0; pi < root._writePaths.length; ++pi)
            cache[root._writePaths[pi]] = settingsController.animationsPage.rawProfile(root._writePaths[pi]) || ({});
        root._pathProfiles = cache;
        root._refreshMirrorDivergence();
    }

    // Build the on-disk profile object from the working state and
    // commit it through the controller. The controller stamps the
    // `name` field automatically.
    function commitOverride() {
        var profile = {
            "duration": root.currentDuration
        };
        // Simple mode hides the timing-mode combo and the Customize dialog, and
        // currentCurveString is seeded from the RESOLVED (inherited) profile
        // when this card owns no override. Writing it unconditionally means
        // dragging Duration alone silently pins a copy of the Global curve here
        // as a direct override — this event then stops tracking later Global
        // curve changes, and simple mode shows no control hinting that a curve
        // override exists or how to clear it. Carry the curve only when the
        // card already owns one, or when the user can actually see and edit it.
        if (!root.simpleTiming) {
            // Advanced mode edits the curve directly, so it always travels.
            root._setOverrideMerged(profile, root.currentCurveString);
            return;
        }
        // Simple mode: the curve is not editable here and currentCurveString
        // was seeded from the RESOLVED profile, so writing it would pin a copy
        // of the inherited curve as a direct override. Decide PER PATH rather
        // than for the group: a path that already owns a curve keeps its own,
        // and a path that inherits keeps inheriting. Deciding once for the
        // group (either direction) writes one path's answer onto the other and
        // splits a group the card presents as one.
        root._setOverrideMerged(profile, undefined);
    }

    // Current emitters that pass empty-path: `shaderProfileChanged`
    // ONLY (fires with `QString()` on full-tree reload). Every other
    // emitter (e.g. `overrideChanged`) always passes a real path.
    // The empty-path carve-out is forward-defense for any future
    // global-broadcast emitter so signal handlers don't silently miss
    // a tree-wide reload.
    function _pathAffectsThisCard(path) {
        if (path === "")
            return true;

        // Mirrors are included even though the card never READS them: the
        // divergence banner's state depends on their stored values, so an
        // edit landing on a mirror alone (the same event's own card on the
        // advanced page) has to re-run this card's refresh or the banner goes
        // stale. Refreshing costs a re-read of the unchanged primary.
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
        // Validate declared mirrors once. A misspelled path is rejected by
        // every writer, so the mirror silently never receives an edit and its
        // stored state can never match the primary's — which latches the
        // divergence banner on with no edit able to clear it. Failing loudly
        // here turns a permanent mystery banner into a one-line console
        // message naming the bad path.
        for (var i = 0; root.mirrorPaths && i < root.mirrorPaths.length; ++i) {
            if (!settingsController.animationsPage.isValidEventPath(root.mirrorPaths[i]))
                console.warn("AnimationEventCard(" + root.eventPath + "): mirrorPaths entry '" + root.mirrorPaths[i] + "' is not a valid event path; writes to it will be silently rejected");
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
        // Toggle OFF semantic: clear timing override AND write
        // an inheritance-blocking shader override. Plain
        // `clearShaderOverride` only removes the entry at this
        // path, leaving inheritance from an ancestor (e.g.
        // `panel` -> "dissolve") to cascade down — exactly the
        // user-reported "I disabled all popups but dissolve
        // still plays" bug. `setShaderOverride(path, "", {})`
        // writes an engaged-empty effectId that
        // `ShaderProfile::overlay` treats as "explicitly no
        // shader", winning over the parent's effectId and
        // blocking the cascade. Same call works for parent
        // cards (popup, window, osd, etc.) so a single
        // OFF toggle on the parent disables every descendant
        // that doesn't have its own override.
        // ── Shared timing + shader editor body ────────────────────
        // All the inline timing controls (curve thumbnail,
        // Customize… button, timing-mode combo, duration slider)
        // and shader controls (picker + parameter editor + color
        // dialog + curve dialog) used to live here. They've been
        // hoisted into the reusable `AnimationProfileEditor` so
        // both this card and the App Rules page can share one
        // implementation. See `AnimationProfileEditor.qml`.
        // The editor's working-state properties (timingMode,
        // duration, easingCurve, springOmega, springZeta,
        // shaderEffectId, shaderParams, lockedShaderParams) are
        // exposed back through this card via property aliases at
        // the top of the file, so `refreshFromTree`,
        // `commitOverride`, `_writeShaderParam`, and the
        // controller signal handlers continue to read and write
        // through the same names as before.

        id: card

        anchors.fill: parent
        headerText: root.eventLabel
        showToggle: true
        toggleChecked: root.overrideEnabled
        // The toggle owns the TIMING override only. Gating the body on it
        // would disable and hide every row including the shader picker, so a
        // user could not drop a shader on an event without first creating a
        // timing override they did not want — and toggling back off would then
        // write the blocking sentinel and wipe the shader again. The timing
        // half is gated on its own, by showTimingSection below.
        gateBodyOnToggle: false
        collapsible: root.collapsible
        onToggleClicked: function (checked) {
            if (checked) {
                root.commitOverride();
            } else {
                // Ordering: write the inheritance-blocking shader
                // sentinel FIRST, then clear the timing override. If
                // the timing clear ever fails mid-flight (e.g. QFile
                // out-of-disk error inside `clearOverride`'s on-disk
                // write), the shader sentinel is already persisted —
                // the user's "disable" intent is recorded in the
                // shader tree even if the timing-side write rolls back.
                // The reverse order would record neither on a partial
                // failure, dropping the disable intent entirely.
                if (root._shaderLegSupported)
                    root._setShaderOverrideOnAll("", ({}));

                root._clearOverrideOnAll();
            }
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            // ── Inheritance info ──────────────────────────────────────
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                type: Kirigami.MessageType.Information
                visible: root.isParentNode ? root.overrideEnabled : !root.overrideEnabled
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
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                type: Kirigami.MessageType.Warning
                visible: root.isParentNode && root._shadowingChildrenCount > 0
                text: i18np("%n descendant event has a shader override that shadows this parent.", "%n descendant events have shader overrides that shadow this parent.", root._shadowingChildrenCount)
                actions: [
                    Kirigami.Action {
                        text: i18n("Clear shadowing children")
                        icon.name: "edit-clear-all"
                        onTriggered: {
                            root._clearShaderOverrideDescendantsOnAll();
                        }
                    }
                ]
            }

            // ── Mirror divergence warning (mirrored cards only) ───────
            // This card writes every mirror path but reads only the primary,
            // so a mirror given its own value elsewhere is not shown by any
            // control here and the next edit replaces it. Warn before that
            // happens rather than after.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                type: Kirigami.MessageType.Warning
                visible: root._mirrorsDiverged
                // Scoped to what a duration edit ACTUALLY converges. The shader leg
                // moves only when the user touches the shader picker, so
                // promising it here left the banner lit after the most likely
                // next action. Count is the number of events the card controls
                // (itself plus its mirrors), not the mirror count.
                text: i18np("Another event this card controls is set differently right now, and this card shows only one of them. The next change you make to the timing here applies to both.", "This card controls %n events that are set differently right now, and it shows only one of them. The next change you make to the timing here applies to all of them.", 1 + root.mirrorPaths.length)
            }

            Label {
                Layout.fillWidth: true
                // Inset to match the rows / banners in this card instead of
                // hugging the left edge.
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: !root.overrideEnabled
                text: i18n("Current: %1", root.inheritSummaryText())
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
            }

            // Section visibility splits the per-axis behaviour the
            // inline layout used to encode: the timing section only
            // surfaces when the master override toggle is on, while
            // the shader section is visible whenever the event
            // supports a shader leg (independent of the timing
            // override — picking a shader doesn't require enabling
            // the timing override).
            AnimationProfileEditor {
                id: editor

                Layout.fillWidth: true
                eventLabel: root.eventLabel
                shaderLegSupported: root._shaderLegSupported
                showTimingSection: root.overrideEnabled
                simpleTiming: root.simpleTiming
                registryRevision: root._shaderRegistryRev
                enableLocking: true
                enableRandomize: true
                enableImage: false
                // Live commit on any timing-axis change — the slider's
                // 30 Hz drag fires `valueChanged` on every move, which
                // routes through `commitOverride()` to write the
                // merged Profile JSON exactly the way the inline
                // version did.
                onValueChanged: {
                    if (root.overrideEnabled)
                        root.commitOverride();
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
                    // sentinel; otherwise switching effect (or promoting an
                    // inherited value to a direct override) drops the
                    // previous effect's parameter map.
                    root._setShaderOverrideOnAll(sid, ({}));
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
                    // through the batch writer (single setShaderOverride
                    // call carrying every roll).
                    root._writeAllShaderParams(root.currentShaderEffectId, rolled);
                }
                onResetRequested: function (defaults) {
                    // Same batch path as randomize — one setShaderOverride
                    // carrying every param at its default.
                    root._writeAllShaderParams(root.currentShaderEffectId, defaults);
                }
            }
        }
    }
}
