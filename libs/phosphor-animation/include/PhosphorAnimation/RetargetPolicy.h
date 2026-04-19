// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

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
enum class PHOSPHORANIMATION_EXPORT RetargetPolicy {
    /// Carry `state.velocity` across the segment boundary, re-scaled
    /// from the old segment's world-distance to the new one. Position
    /// is continuous. This is the default — consumers that drag a
    /// window through multiple zones mid-snap expect "no visible stall"
    /// which maps to velocity preservation. Stateless curves downgrade
    /// to `PreservePosition` + a debug log.
    PreserveVelocity,

    /// Zero `state.velocity` on retarget. Position continuous; the
    /// motion restarts from zero rate on the new segment. Useful when
    /// the caller wants the new target to feel like a fresh gesture
    /// rather than a redirection of an existing impulse (e.g., a
    /// workspace-switch animation interrupted by a click that should
    /// start from rest toward the click target).
    ResetVelocity,

    /// Preserve position only; velocity treatment is the curve's choice.
    /// For stateful curves this zeroes velocity (equivalent to
    /// `ResetVelocity`); for stateless curves this is the natural
    /// retarget behaviour. Exists so consumers can state the minimal
    /// continuity guarantee they require without over-constraining the
    /// curve's velocity semantics.
    PreservePosition,
};

} // namespace PhosphorAnimation
