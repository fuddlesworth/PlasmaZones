// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief The card's side of a group write: the re-entrancy latch and the
 *        refresh that follows.
 *
 * The write POLICY itself now lives in C++, as the `*OnPaths` group-write
 * Q_INVOKABLEs on AnimationsPageController (see the "Group writes" block in
 * animationspagecontroller.h). The per-field merge rules were the same
 * drop-versus-substitute semantics `rawProfile` already implements, and keeping
 * them in two languages meant two places to hold in step — the settings mirror
 * and the library validator drifting apart is exactly the class of bug that
 * motivated the move.
 *
 * What is left here is genuinely the card's: while its own write is in flight,
 * the card must ignore the signals that write emits (`_committing` /
 * `_committingShader`), and afterwards it must refresh once. Neither means
 * anything C++-side. Every WRITER below is therefore the same shape — latch,
 * one controller call, unlatch and refresh — and each still exists separately
 * so `mirrorPaths` cannot be bypassed by a future call site reaching a per-path
 * mutator directly. The shape is not identical across all of them: the timing
 * writers refresh once and bump `_inheritRev`, while the shader writers refresh
 * BOTH axes, because a shader write can change what the timing side inherits.
 *
 * The queries at the end of the file carry neither latch nor refresh, because
 * none of them moves anything in the STORE for a signal to report.
 * `_anyWritePathSupportsShaderLeg` and `_allWritePathsHold` are pure reads.
 * `_refreshMirrorDivergence` is not — it assigns the card's `_mirrorsDiverged`
 * and `_divergentPathCount` — but what it writes is view state the card owns,
 * so it needs no latch either.
 *
 * The try/finally is not decoration. QML has no RAII, and a throw inside a
 * write (a Q_INVOKABLE argument conversion, or `settingsController` resolving
 * undefined during a page teardown) would otherwise leave the latch TRUE for
 * the rest of the session: `onOverrideChanged` would early-return forever, so
 * the card would stop tracking Discard, profile switches and external edits
 * while still writing on every slider tick, and the list's Loaders latch built
 * and never unload, so it is never reconstructed to recover. The refresh is in
 * the `finally` for the same reason — every signal the write emitted was
 * deliberately swallowed on the promise that one refresh follows it.
 */
QtObject {
    id: writers

    /// The AnimationEventCard these writers belong to.
    required property var card

    // ── Timing group writes ─────────────────────────────────────────
    /// Merge `profile` into every write path's stored override.
    ///
    /// `curveFromCommit` is a curve the user actually edited, so it travels to
    /// every path. Pass `undefined` whenever the user did not edit the curve (a
    /// duration commit, or simple mode where no curve control exists): each
    /// path then keeps its own, so a path that owns one has it preserved and a
    /// path that inherits stays inheriting. The card must not decide a curve on
    /// the user's behalf. `undefined` arrives C++-side as an invalid QVariant,
    /// which is how that distinction survives the call. Spell it explicitly:
    /// OMITTING the argument is not the same thing, it raises
    /// `Error: Insufficient arguments` and the write never happens.
    function _setOverrideMerged(profile, curveFromCommit) {
        card._committing = true;
        try {
            // Return whether the write landed (>= 0), like every sibling writer
            // here. -1 means the controller REFUSED and nothing was attempted,
            // which is the one thing the card's latch should stop re-issuing; a
            // partial failure toasts on its own and reports the count that did
            // land, so the user can keep trying.
            return settingsController.animationsPage.setOverrideMergedOnPaths(card._writePaths, profile, curveFromCommit) >= 0;
        } finally {
            card._committing = false;
            card._inheritRev++;
            card.refreshFromTree(true);
        }
    }

    /// Return ONE timing field (`"curve"` or `"duration"`) to inheritance on
    /// every write path, leaving the other field and the motion-set fields put.
    ///
    /// False when the controller returned -1: the call was refused (an async
    /// discard is in flight) or some write failed, and it toasted the reason.
    /// A caller that shows its own feedback must honour it rather than report a
    /// revert that did not happen.
    function _clearFieldOnAll(field) {
        card._committing = true;
        try {
            return settingsController.animationsPage.clearFieldOnPaths(card._writePaths, field) >= 0;
        } finally {
            card._committing = false;
            card._inheritRev++;
            card.refreshFromTree(true);
        }
    }

    /// Remove the override on every write path outright, in ONE controller
    /// call rather than clearOverride in a loop: the batch entry point deletes
    /// every file, rescans the profile registry ONCE, and emits one dirty
    /// signal for the net flip.
    ///
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
            card.refreshFromTree(true);
        }
    }

    // ── Shader group writes ─────────────────────────────────────────
    // These refresh BOTH trees: a shader write moves the shader tree, and the
    // timing refresh is what re-derives the card's Override toggle, which
    // reports either axis.
    /// Set the shader override on every write path that can host a shader leg.
    /// Reached from the PICKER, not from the param sliders: choosing a pack is
    /// the one action that should stamp an id. The sliders go through
    /// `_setShaderParamsOnAll` below, which is what leaves an inheriting
    /// event's pack alone. The controller still applies the whole group to one
    /// tree read and one write.
    ///
    /// False when the controller returned -1: an async discard owns the tree
    /// and it toasted the reason.
    function _setShaderOverrideOnAll(effectId, params) {
        card._committingShader = true;
        try {
            return settingsController.animationsPage.setShaderOverrideOnPaths(card._writePaths, effectId, params) >= 0;
        } finally {
            card._committingShader = false;
            card.refreshShaderFromTree();
            card.refreshFromTree(true);
        }
    }

    /// Set the shader PARAMETERS on every write path, leaving each path's pack
    /// exactly as stored.
    ///
    /// The param sliders use this rather than the setter above, and the
    /// difference matters on a leaf that inherits its pack: the setter stamps
    /// the id it is handed, which for an inheriting leaf is the id it inherited,
    /// so one slider nudge used to pin the parent's pack as a direct override.
    /// The leaf then stopped following the parent, and the parent's card raised
    /// a "descendant shadows this parent" warning whose clear action deleted the
    /// tweak that had just been made.
    function _setShaderParamsOnAll(params) {
        card._committingShader = true;
        try {
            return settingsController.animationsPage.setShaderParametersOnPaths(card._writePaths, params) >= 0;
        } finally {
            card._committingShader = false;
            card.refreshShaderFromTree();
            card.refreshFromTree(true);
        }
    }

    /// Clear the shader override on every write path, returning the event to
    /// inheritance. Distinct from writing the engaged-empty sentinel, which is
    /// an explicit "None" that BLOCKS inheritance — that is the picker's job,
    /// not the toggle's.
    /// False when the controller returned -1 (an async discard owns the tree,
    /// and it toasted the reason).
    function _clearShaderOverrideOnAll() {
        card._committingShader = true;
        try {
            return settingsController.animationsPage.clearShaderOverrideOnPaths(card._writePaths) >= 0;
        } finally {
            card._committingShader = false;
            card.refreshShaderFromTree();
            card.refreshFromTree(true);
        }
    }

    /// Clear the shader overrides BELOW every write path. Returns the number
    /// cleared, or -1 if a path refused (the controller's "async discard in
    /// flight" sentinel), which the caller must not read as a smaller
    /// successful clear.
    function _clearShaderOverrideDescendantsOnAll() {
        card._committingShader = true;
        try {
            return settingsController.animationsPage.clearShaderOverrideDescendantsOnPaths(card._writePaths);
        } finally {
            card._committingShader = false;
            card.refreshShaderFromTree();
            card.refreshFromTree(true);
        }
    }

    // ── Group queries ───────────────────────────────────────────────
    // No latch and no refresh: none of these moves anything in the STORE. The
    // first two are pure reads; `_refreshMirrorDivergence` at the end assigns
    // two of the card's own view properties, which is why the group is not
    // called read-only.
    /// True when ANY write path takes a shader leg. Gating a group mutation on
    /// the primary alone would skip a mirror that does support one, whose
    /// shader override would then survive the toggle and show as a divergence
    /// no control could clear.
    function _anyWritePathSupportsShaderLeg() {
        return settingsController.animationsPage.anyPathSupportsShaderLeg(card._writePaths);
    }

    /// True iff every write path already carries `effectId` as its DIRECT
    /// shader override (the empty string being the engaged-empty sentinel,
    /// which is distinct from "no override at all").
    function _allWritePathsHold(effectId) {
        return settingsController.animationsPage.allPathsHoldShaderEffect(card._writePaths, effectId);
    }

    /// Recompute `_mirrorsDiverged` and `_divergentPathCount`. Called from
    /// refreshFromTree (which every shader-side refresh chains into), so it
    /// tracks every signal that can move either tree.
    ///
    /// The curve is compared only outside simple mode, because simple mode has
    /// no curve control at all: no edit there could converge a divergent mirror
    /// curve, and counting it would latch the banner ON permanently over an
    /// axis nothing on the card can clear.
    function _refreshMirrorDivergence() {
        const count = settingsController.animationsPage.divergentPathCount(card.eventPath, card._validMirrorPaths, !card.simpleTiming);
        card._mirrorsDiverged = count > 0;
        card._divergentPathCount = count;
    }
}
