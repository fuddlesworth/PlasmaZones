// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Interpolate.h> // kRectSizeEpsilonPx
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/RetargetPolicy.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QRectF>

#include <optional>

namespace PhosphorAnimation {

class IMotionClock;

/**
 * @brief Snap-animation policy gate — "is this transition worth animating?".
 *
 * Phase 3 replacement for the Phase-2 `AnimationMath::createSnapMotion`.
 * Extracts the skip-rules (degenerate target, sub-threshold move without
 * scale change) into a pure function that returns a fully-populated
 * `MotionSpec<QRectF>` ready to feed into `AnimatedValue<QRectF>::start()`.
 *
 * This is the thin layer that mediates between a *snap* action (a
 * discrete "snap window to zone" intent) and the generic animation
 * runtime. Non-snap consumers (QML `Behavior`, shader uniforms, zone
 * fades) build `MotionSpec<T>` directly from their own logic and do
 * not go through `SnapPolicy`.
 */
namespace SnapPolicy {

/// Parameters for the snap gate.
///
/// `profile.minDistance` is the single source of truth for the skip
/// threshold — `createSnapSpec` reads `profile.effectiveMinDistance()`
/// directly. There is no separate `minDistance` field on `SnapParams`:
/// persist the value on the `Profile` (where it round-trips through
/// JSON / the settings UI) and stamp it into this struct via one
/// `profile` copy.
struct SnapParams
{
    /// The full Profile to stamp into the animation's MotionSpec.
    /// Carries curve, duration, minDistance, sequenceMode,
    /// staggerInterval, and any future orchestration fields. The
    /// spec's profile inherits verbatim — unset optionals resolve via
    /// `Profile::effective*()` at animation-time, not here.
    Profile profile;

    /// Default retarget policy to stamp into the resulting MotionSpec.
    /// `PreserveVelocity` matches the drag-through-zones expectation
    /// (no visible stall mid-retarget). Callers that want a different
    /// default — e.g. a workspace-switch interrupted by a click,
    /// which feels more natural with `ResetVelocity` so the new
    /// target starts from rest — set this explicitly.
    RetargetPolicy retargetPolicy = RetargetPolicy::PreserveVelocity;
};

/// Canonical size-change epsilon shared between the snap gate and
/// adapter-side "should I apply scale this paint?" checks
/// (`AnimatedValue<QRectF>::hasSizeChange`). Both sides use 1.0 px so
/// a sub-pixel size delta is treated identically by both layers.
///
/// Aliases `PhosphorAnimation::kRectSizeEpsilonPx` (defined in
/// AnimatedValue.h). Single source of truth on the generic layer; the
/// snap-flavoured name is kept here for call-site clarity.
inline constexpr qreal kSnapSizeEpsilonPx = kRectSizeEpsilonPx;

/**
 * @brief Build a `MotionSpec<QRectF>` if the transition merits animation.
 *
 * Returns `std::nullopt` when:
 *   - @p clock is null (no runtime to drive the animation);
 *   - @p newFrame is degenerate (zero or negative size);
 *   - position delta is below `max(1, profile.effectiveMinDistance())`
 *     AND the frame size is unchanged (the animation would not be
 *     visible).
 *
 * Otherwise returns a `MotionSpec<QRectF>` populated with:
 *   - `profile` = @p params.profile (full copy — curve, duration,
 *     minDistance, sequence mode, etc. all propagate)
 *   - `clock` = @p clock
 *   - `retargetPolicy` = @p params.retargetPolicy
 *   - callbacks left unset (caller wires onValueChanged / onComplete)
 *
 * The returned spec has no `onValueChanged` or `onComplete` callback —
 * the caller (typically `AnimationController`) attaches those based on
 * its own lifecycle needs.
 */
PHOSPHORANIMATION_EXPORT std::optional<MotionSpec<QRectF>>
createSnapSpec(const QRectF& oldFrame, const QRectF& newFrame, const SnapParams& params, IMotionClock* clock);

} // namespace SnapPolicy

} // namespace PhosphorAnimation
