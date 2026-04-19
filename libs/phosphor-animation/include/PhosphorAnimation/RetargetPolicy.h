// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace PhosphorAnimation {

/**
 * @brief How an in-flight `AnimatedValue<T>` should be reshaped when
 *        `retarget()` is called mid-animation.
 *
 * The concrete behaviour depends on the underlying curve's capability
 * (see Phase 3 decision C in the design doc):
 *
 * - **Stateful curves (`Spring`):** all three variants behave distinctly.
 *   All three preserve position (the value does not jump). The three
 *   variants differ in how `CurveState::velocity` is treated across the
 *   segment boundary.
 * - **Stateless curves (`Easing`):** `PreservePosition` and
 *   `ResetVelocity` converge to the same behaviour (there is no
 *   physical velocity to preserve on a parametric curve). The
 *   `PreserveVelocity` request degrades to `PreservePosition` and logs
 *   a debug hint so callers writing curve-agnostic retarget loops can
 *   see the downgrade rather than silently getting a different motion.
 */
enum class RetargetPolicy {
    /// Carry `state.velocity` across the segment boundary, re-scaled
    /// from the old segment's world-distance to the new one. Position
    /// is continuous. This is the default — consumers that drag a
    /// window through multiple zones mid-snap expect "no visible stall"
    /// which maps to velocity preservation. Stateless curves downgrade
    /// to `PreservePosition` + a debug log.
    ///
    /// ### Caveat for `AnimatedValue<QTransform>`
    ///
    /// The Frobenius-norm distance used to rescale velocity mixes
    /// units (translation-pixels and rotation-radians / scale-factors
    /// all contribute equally), so on mixed translate+rotate+scale
    /// retargets at non-trivial speed the rescaled velocity would be
    /// physically meaningless. `AnimatedValue<QTransform>::retarget`
    /// auto-detects this — when any of the segment endpoints has a
    /// non-identity linear part, it silently downgrades
    /// `PreserveVelocity` to `PreservePosition` and logs a debug hint
    /// once per instance. Pure-translate transforms (the common case —
    /// window snap, scroll offsets) are handled on the normal
    /// velocity-preserving path.
    ///
    /// ### Caveat for `AnimatedValue<QRectF>` on size-dominated motion
    ///
    /// `Interpolate<QRectF>::distance` is a 4-D Euclidean norm over
    /// `(x, y, w, h)` — position-pixels and size-pixels both contribute
    /// equally per axis. For the typical snap use case
    /// (position-dominated; size delta is ≤ the gap between adjacent
    /// zones and ≪ the translate across the workspace) this is fine:
    /// the velocity rescale is effectively a translate-rate rescale
    /// with a small size-rate correction. Consumers of
    /// `AnimatedValue<QRectF>` whose animations are size-dominated
    /// (grid-cell resize, expanding tooltips, layouts where the
    /// position delta is tiny relative to the size change) will observe
    /// a rescaled velocity that confuses "pixels of translate per
    /// second" with "pixels of resize per second". There is no
    /// auto-degrade here — the gate would require introspecting the
    /// old and new segments' translate-vs-size ratio and picking a
    /// threshold that's inherently workload-specific.
    ///
    /// Size-dominated consumers SHOULD either pass `PreservePosition`
    /// / `ResetVelocity` explicitly on retarget, or split the motion
    /// into two separate `AnimatedValue` instances — one for
    /// `QPointF`, one for `QSizeF` — whose distance metrics are pure.
    /// The SnapPolicy + AnimationController flow stays on
    /// `AnimatedValue<QRectF>` with `PreserveVelocity` as default
    /// because window snap is position-dominated by construction.
    PreserveVelocity,

    /// Zero `state.velocity` on retarget. Position continuous; the
    /// motion restarts from zero rate on the new segment. Useful when
    /// the caller wants the new target to feel like a fresh gesture
    /// rather than a redirection of an existing impulse (e.g., a
    /// workspace-switch animation interrupted by a click that should
    /// start from rest toward the click target).
    ///
    /// Intent: "the impulse that drove the old segment is finished;
    /// reset physics state before heading toward the new target."
    /// The controller explicitly demands zero velocity on the new
    /// segment regardless of curve family.
    ResetVelocity,

    /// Preserve position only; velocity treatment is delegated to the
    /// curve's natural retarget behaviour. For stateful curves (Spring)
    /// this zeroes velocity — identical observed behaviour to
    /// `ResetVelocity` in the current implementation — but the intent
    /// differs: the caller is stating "I don't care what the velocity
    /// does, just don't visually jump" rather than actively requesting
    /// a reset. For stateless curves (Easing) this is always the
    /// natural retarget behaviour (there is no physical velocity to
    /// preserve or discard).
    ///
    /// Prefer this over `ResetVelocity` when you need a minimal
    /// continuity guarantee; prefer `ResetVelocity` when the caller's
    /// domain model explicitly demands a velocity reset (e.g., a
    /// stateful gesture ending). A future curve family could meaningfully
    /// diverge the two (e.g., a curve that preserves *direction* under
    /// `PreservePosition` but zeroes *magnitude* under `ResetVelocity`).
    PreservePosition,
};

} // namespace PhosphorAnimation
