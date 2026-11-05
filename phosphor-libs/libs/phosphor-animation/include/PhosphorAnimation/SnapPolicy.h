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

/// Snap-animation policy gate — decides if a transition merits animation
/// and builds a MotionSpec<QRectF> for AnimatedValue::start().
namespace SnapPolicy {

struct SnapParams
{
    Profile profile;
    RetargetPolicy retargetPolicy = RetargetPolicy::PreserveVelocity;
};

inline constexpr qreal kSnapSizeEpsilonPx = kRectSizeEpsilonPx;

/// Returns nullopt when clock is null, newFrame is degenerate, or the move
/// is below threshold with no size change. Otherwise returns a MotionSpec
/// with no callbacks (caller wires those).
PHOSPHORANIMATION_EXPORT std::optional<MotionSpec<QRectF>>
createSnapSpec(const QRectF& oldFrame, const QRectF& newFrame, const SnapParams& params, IMotionClock* clock);

} // namespace SnapPolicy

} // namespace PhosphorAnimation
