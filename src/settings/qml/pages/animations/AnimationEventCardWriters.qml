// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief The write policy for AnimationEventCard, hoisted out of it.
 *
 * Every controller write a card performs goes through one of these, so
 * `mirrorPaths` cannot be silently bypassed by a future call site, and so the
 * re-entrancy latches (`_committing` / `_committingShader`) are applied in one
 * place rather than per call site.
 *
 * Split out for the same reason `ActionParamEditors` was split out of
 * `ActionRow`: the card was at the file-size ceiling, and squeezing prose to
 * stay under it is not durable headroom. The bodies are UNCHANGED apart from
 * `root.` becoming `card.` — this object owns no state of its own, it reads and
 * writes the card's.
 */
QtObject {
    id: writers

    /// The AnimationEventCard these writers belong to. Named `card` so the
    /// moved bodies resolve against it with no other rewrites.
    required property var card

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
        card._committingShader = true;
        try {
            const paths = card._writePaths;
            for (var i = 0; i < paths.length; ++i) {
                // Skip paths with no shader leg, mirroring the toggle-off guard
                // (_anyWritePathSupportsShaderLeg). setShaderOverride already
                // rejects such a path, so this only avoids a known no-op call and
                // its qCWarning. The divergence-latch it would otherwise cause on
                // a mixed mirror set is prevented in _storedStateKey, which omits
                // the shader axis for non-supporting paths — the two guards
                // together keep the banner off for a set mixing supporting and
                // non-supporting paths. A no-op for the current mirror set (both
                // window.appearance legs support shaders).
                if (!settingsController.animationsPage.supportsShaderLeg(paths[i]))
                    continue;
                settingsController.animationsPage.setShaderOverride(paths[i], effectId, params);
            }
        } finally {
            // Same try/finally reasoning as _setOverrideMerged: QML has no
            // RAII, and a latched flag here would stop the card tracking
            // external shader edits for the rest of the session.
            card._committingShader = false;
            card.refreshShaderFromTree();
            card.refreshFromTree();
        }
    }

    /// Writes `profile` to every path this card controls, merged over each
    /// path's OWN stored profile so fields this card does not edit
    /// (minDistance, sequenceMode, staggerInterval, presetName) survive
    /// instead of being truncated. A motion set can write those to a leaf
    /// (see motionsetdomain.cpp), and a card that overwrote the whole map
    /// would silently drop them the moment the user nudged Duration.
    ///
    /// `curveFromCommit` is a curve the user has actually edited, so it
    /// travels to every path. Pass `undefined` whenever the user did not
    /// edit the curve (a duration commit, or simple mode where no curve
    /// control exists): each path then keeps its OWN curve, so a path that
    /// owns one has it preserved and a path that inherits stays inheriting.
    /// The card must not decide a curve on the user's behalf.
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
        card._committing = true;
        try {
            writers._setOverrideMergedLoop(profile, curveFromCommit);
        } finally {
            // Both the flag AND the refresh are in the finally. Every
            // overrideChanged the loop emitted was deliberately swallowed on
            // the promise that one refresh follows it, so a throw that skipped
            // the refresh would break exactly the invariant the flag exists to
            // defend: the writes that DID land would never reach the card, and
            // _pathProfiles / overrideEnabled / the divergence banner would
            // stay stale until some unrelated signal arrived.
            card._committing = false;
            card._inheritRev++;
            card.refreshFromTree();
        }
    }

    /// The write loop itself. Split out so _setOverrideMerged's try/finally
    /// reads as one statement and the flag's lifetime is obvious.
    function _setOverrideMergedLoop(profile, curveFromCommit) {
        const paths = card._writePaths;
        for (var i = 0; i < paths.length; ++i) {
            // Same fallback as _clearFieldOnAll: a path absent from the
            // cache is re-read rather than treated as empty, because merging
            // onto {} would truncate that path's other motion-set fields —
            // the exact loss this merged writer exists to prevent.
            var raw = card._pathProfiles[paths[i]] || settingsController.animationsPage.rawProfile(paths[i]) || ({});
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

    /// True when ANY write path takes a shader leg. _shaderLegSupported answers
    /// for the PRIMARY only, so gating a group mutation on it would skip a
    /// mirror that does support one: that mirror's shader override would
    /// survive the toggle, and _storedStateKey compares the shader map
    /// unconditionally, so the divergence banner would latch on with no control
    /// able to clear it. Matches the group-writer shape of every other mutation
    /// on this card.
    function _anyWritePathSupportsShaderLeg() {
        const paths = card._writePaths;
        for (var i = 0; i < paths.length; ++i) {
            if (settingsController.animationsPage.supportsShaderLeg(paths[i]))
                return true;
        }
        return false;
    }

    /// Clears the shader override on every write path, returning the event to
    /// inheritance. Distinct from writing the engaged-empty sentinel, which is
    /// an explicit "None" that BLOCKS inheritance — that is the picker's job,
    /// not the toggle's.
    function _clearShaderOverrideOnAll() {
        card._committingShader = true;
        try {
            const paths = card._writePaths;
            for (var i = 0; i < paths.length; ++i)
                settingsController.animationsPage.clearShaderOverride(paths[i]);
        } finally {
            card._committingShader = false;
            card.refreshShaderFromTree();
            card.refreshFromTree();
        }
    }

    /// One controller call for the whole group, not clearOverride in a loop:
    /// the batch entry point deletes every file, rescans the profile registry
    /// ONCE, and emits one dirty signal for the net flip. Looping the
    /// single-path call instead paid a full directory rescan and re-parse per
    /// mirror.
    ///
    /// Still suppressed with _committing: the batch emits one overrideChanged
    /// per cleared path, and an unguarded handler would recompute the
    /// divergence banner against a half-refreshed card, flickering it on
    /// mid-batch.
    /// False when the controller returned -1: an async discard is in flight, OR
    /// some file could not be removed. Both raise a toast C++-side, so the
    /// caller must honour it — a partial failure deliberately keeps the editor
    /// open rather than reporting success it did not achieve.
    function _clearOverrideOnAll() {
        card._committing = true;
        try {
            return settingsController.animationsPage.clearOverridesUnder(card._writePaths) >= 0;
        } finally {
            card._committing = false;
            card._inheritRev++;
            card.refreshFromTree();
        }
    }

    /// Removes ONE timing field (`"curve"` or `"duration"`) from every write
    /// path's stored override, returning that field to inheritance while the
    /// other field (and the motion-set fields) stay put. A path whose override
    /// becomes empty has its file deleted outright: an empty override file and
    /// no override resolve identically, but the toggle and the pending-changes
    /// walk both key on file existence. Suppressed and
    /// group-written like every other mutation on this card.
    function _clearFieldOnAll(field) {
        card._committing = true;
        try {
            const paths = card._writePaths;
            for (var i = 0; i < paths.length; ++i) {
                var raw = Object.assign({}, card._pathProfiles[paths[i]] || settingsController.animationsPage.rawProfile(paths[i]) || ({}));
                if (raw[field] === undefined)
                    continue;
                delete raw[field];
                if (Object.keys(raw).length === 0)
                    settingsController.animationsPage.clearOverride(paths[i]);
                else
                    settingsController.animationsPage.setOverride(paths[i], raw);
            }
        } finally {
            card._committing = false;
            card._inheritRev++;
            card.refreshFromTree();
        }
    }

    // Returns the number cleared, or -1 if a path refused (the controller's
    // "async discard in flight" sentinel). Summing a -1 in would make a refusal
    // indistinguishable from a smaller successful clear.
    //
    // _committingShader for the same reason as the two timing group writers:
    // each clear writes the shader tree, relayed as a path-agnostic
    // shaderProfileChanged, and an unguarded handler would recompute the
    // shadowing count and divergence banner against a half-cleared tree.
    // Stops at the first refusal — the controller toasts per REFUSED call, and the
    // in-flight gate cannot change between iterations.
    function _clearShaderOverrideDescendantsOnAll() {
        card._committingShader = true;
        try {
            const paths = card._writePaths;
            var cleared = 0;
            for (var i = 0; i < paths.length; ++i) {
                const n = settingsController.animationsPage.clearShaderOverrideDescendants(paths[i]);
                if (n < 0)
                    return -1;
                cleared += n;
            }
            return cleared;
        } finally {
            card._committingShader = false;
            card.refreshShaderFromTree();
            card.refreshFromTree();
        }
    }

    /// True iff every write path already carries @p effectId as its DIRECT
    /// shader override (the empty string being the engaged-empty sentinel,
    /// which is distinct from "no override at all").
    function _allWritePathsHold(effectId) {
        const paths = card._writePaths;
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
    /// Prefers the _pathProfiles / _pathShaderProfiles snapshots when they hold
    /// this path: both are refreshed on the same pass that calls this, and both
    /// underlying reads are expensive per call (a synchronous file open, and a
    /// full shader-tree parse), so reading either here defeated the caches'
    /// whole purpose on every drag tick. The uncached reads remain as the
    /// fallback for a path the snapshots have not seen.
    function _storedStateKey(path) {
        const cached = card._pathProfiles[path];
        const profile = cached || settingsController.animationsPage.rawProfile(path) || ({});
        // Shader axis only for paths that can actually host a shader leg. A
        // non-supporting path always stores {} (the controller rejects a shader
        // write to it, and _setShaderOverrideOnAll skips it), so comparing its
        // permanently-empty shader against a supporting path's real one would
        // latch the divergence banner over an axis no control could ever
        // converge. For the current all-supporting mirror set this is a no-op;
        // it only matters for a future set mixing supporting and non-supporting
        // paths. Non-supporting paths contribute a constant {} so they never
        // diverge on this axis.
        const shaderComparable = settingsController.animationsPage.supportsShaderLeg(path);
        const cachedShader = shaderComparable ? card._pathShaderProfiles[path] : undefined;
        const shader = shaderComparable ? (cachedShader || settingsController.animationsPage.rawShaderProfile(path) || ({})) : ({});
        // Divergence is measured on exactly what this card CAN converge with a
        // single edit: duration (commitDurationOverride), the curve
        // (commitCurveOverride) and the whole shader leg
        // (_setShaderOverrideOnAll, reached from the picker, the param
        // sliders, randomize and reset). Every group writer loops
        // _writePaths, so a divergence on any counted axis really is
        // converged by the next edit on that axis, which is what the banner
        // promises. Writes are per field now, so converging the duration no
        // longer converges the curve as a side effect — a curve-only
        // divergence stays (and stays reported) until the user edits the
        // curve itself.
        //
        // The curve is conditional on !simpleTiming because simple mode has
        // no curve control at all: no edit there can converge a divergent
        // mirror curve, and counting it would latch the banner ON permanently
        // over an axis nothing on the card can clear. Advanced mode counts it
        // because commitCurveOverride and the curve revert link both loop
        // every write path.
        //
        // The motion-set fields (minDistance, sequenceMode, staggerInterval,
        // presetName) are left out for the same reason: the merged writer
        // preserves each path's own, so counting them latched the banner with
        // no control able to clear it. An allowlist, so a new stored-profile
        // field cannot latch it again unless the card writes it.
        const compared = {};
        if (profile.duration !== undefined)
            compared.duration = profile.duration;
        if (!card.simpleTiming && profile.curve !== undefined)
            compared.curve = profile.curve;
        return JSON.stringify([compared, shader]);
    }

    /// Recompute _mirrorsDiverged. Called from refreshFromTree (which every
    /// shader-side refresh chains into), so it tracks every signal that can move
    /// either tree.
    function _refreshMirrorDivergence() {
        const mirrors = card._validMirrorPaths;
        if (mirrors.length === 0) {
            card._mirrorsDiverged = false;
            card._divergentPathCount = 0;
            return;
        }
        const primary = writers._storedStateKey(card.eventPath);
        var diverged = 0;
        for (var i = 0; i < mirrors.length; ++i) {
            if (writers._storedStateKey(mirrors[i]) !== primary)
                ++diverged;
        }
        card._mirrorsDiverged = diverged > 0;
        // Plus one for the primary, which every diverging mirror differs FROM
        // and which the converging edit also rewrites. Zero when nothing
        // diverges, so the banner never renders a stale count.
        card._divergentPathCount = diverged > 0 ? diverged + 1 : 0;
    }
}
