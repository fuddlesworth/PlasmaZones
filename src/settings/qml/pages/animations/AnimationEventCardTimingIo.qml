// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.phosphor.animation

/**
 * @brief The card's timing-axis seed and commit bodies.
 *
 * The timing half of the split AnimationEventCardShaderIo made on the shader
 * half, and for the same reason: AnimationEventCard sits against the project's
 * size ceiling, and one axis's read-and-write pair is a coherent slice rather
 * than an arbitrary cut. `_applyEffective` seeds the working controls from a
 * resolved profile; the two commit functions push the user's edits back.
 *
 * The card keeps a thin forwarder for each, so every call site still reads
 * `root._foo(...)`.
 *
 * The asymmetry between the two commits mirrors the shader side's and is just
 * as easy to "tidy" into a bug. Duration is drag-rate, so a refused write is
 * dropped without restoring the slider. Curve is discrete, so a refused write
 * DOES restore: there is no later tick to correct it.
 */
QtObject {
    id: timingIo

    /// The AnimationEventCard these bodies belong to.
    required property var card

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
            card.currentTimingMode = CurvePresets.timingModeSpring;
            card.currentSpringOmega = s.omega;
            card.currentSpringZeta = s.zeta;
        } else {
            card.currentTimingMode = CurvePresets.timingModeEasing;
            // With a default fallback, matching the duration line below and
            // inheritSummaryText's read of the same value: without the else,
            // an absent curve (mid-warmup {} resolution, or resolvedProfile's
            // empty-path early return) kept the property's PREVIOUS value —
            // which after a revert is exactly the just-reverted curve, the
            // stale-view class this card's fix exists to close — while the
            // italic "Current:" line already showed the default.
            card.currentEasingCurve = (typeof curve === "string" && curve.length > 0) ? curve : CurvePresets.defaultEasingCurve;
        }
        card.currentDuration = effective.duration !== undefined ? effective.duration : CurvePresets.defaultDurationMs;
    }

    function commitDurationOverride() {
        // Drag-rate (the slider emits per move), so a refused write is dropped
        // without restoring the slider — refreshing per tick would pull the
        // handle back out from under the user. Same trade as
        // `_writeShaderParam`; `commitCurveOverride` below is discrete and does
        // restore.
        if (card._writesRefused)
            return;

        // The merged writer overlays only the fields in `profile`, and the
        // `undefined` curve means "each path keeps its own curve, or keeps
        // inheriting" — decided PER PATH so a mirror that owns a curve is
        // preserved and one that inherits stays inheriting.
        card._noteWriteResult(card._setOverrideMerged({
            "duration": card.currentDuration
        }, undefined));
    }

    function commitCurveOverride() {
        // Discrete (a combo activation or the curve dialog accepting), so a
        // refused write leaves the editor showing a curve that never reached
        // disk with no later tick to correct it. Restore the stored state.
        //
        // selfDriven, like every other refresh this card drives itself. Without
        // it the refresh would clear `_writesRefused` — the very latch that
        // just refused this write — and would take the close-the-editor branch
        // if the discard has already emptied the store, folding the timing
        // editor away under the user who just picked the curve.
        if (card._writesRefused) {
            card.refreshFromTree(true);
            return;
        }

        // Empty profile map: nothing but the curve travels. Each path's own
        // stored duration (or its absence) survives the merge untouched.
        card._noteWriteResult(card._setOverrideMerged({}, card.currentCurveString));
    }
}
