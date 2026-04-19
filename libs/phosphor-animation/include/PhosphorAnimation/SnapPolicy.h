// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QRectF>

#include <memory>
#include <optional>

namespace PhosphorAnimation {

class Curve;
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
struct PHOSPHORANIMATION_EXPORT SnapParams
{
    /// Animation duration in milliseconds. Stored as the Profile's
    /// `duration` override — for stateful curves (Spring) duration acts
    /// as a hard cap; for stateless curves it drives the parametric t.
    qreal duration = 150.0;

    /// Curve applied to the animation. `nullptr` = inherit from Profile
    /// defaults (AnimatedValue falls back to OutCubic bezier if no
    /// curve is resolvable).
    std::shared_ptr<const Curve> curve;

    /// Skip threshold in pixels. A position delta below
    /// `max(1, minDistance)` with no size change is skipped as not
    /// worth animating (matches Phase 2 semantics exactly).
    int minDistance = 0;
};

/**
 * @brief Build a `MotionSpec<QRectF>` if the transition merits animation.
 *
 * Returns `std::nullopt` when:
 *   - @p clock is null (no runtime to drive the animation);
 *   - @p newFrame is degenerate (zero or negative size);
 *   - position delta is below `max(1, minDistance)` AND the frame size
 *     is unchanged (the animation would not be visible).
 *
 * Otherwise returns a `MotionSpec<QRectF>` populated with:
 *   - `profile.curve` = @p params.curve
 *   - `profile.duration` = @p params.duration
 *   - `clock` = @p clock
 *   - default `retargetPolicy = PreserveVelocity`
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
