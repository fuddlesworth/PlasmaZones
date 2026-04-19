// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/SnapPolicy.h>

#include <PhosphorAnimation/IMotionClock.h>

#include <QLineF>

namespace PhosphorAnimation {
namespace SnapPolicy {

std::optional<MotionSpec<QRectF>> createSnapSpec(const QRectF& oldFrame, const QRectF& newFrame,
                                                 const SnapParams& params, IMotionClock* clock)
{
    if (!clock) {
        return std::nullopt;
    }
    if (newFrame.width() <= 0.0 || newFrame.height() <= 0.0) {
        return std::nullopt;
    }

    // Position delta (distance between top-left corners) and size-change
    // detection mirror Phase 2's createSnapMotion exactly — preserving
    // the "is this worth animating?" invariant that's been tuned through
    // user feedback.
    //
    // `minDistance` is read from the Profile (single source of truth).
    // The floor of 1.0 px matches Phase 2 — a zero-configured threshold
    // still skips the no-op (oldFrame == newFrame) case.
    const qreal posDistance = QLineF(oldFrame.topLeft(), newFrame.topLeft()).length();
    const bool sizeChanging = qAbs(oldFrame.width() - newFrame.width()) > kSnapSizeEpsilonPx
        || qAbs(oldFrame.height() - newFrame.height()) > kSnapSizeEpsilonPx;
    const qreal threshold = qMax(1.0, qreal(params.profile.effectiveMinDistance()));
    if (posDistance < threshold && !sizeChanging) {
        return std::nullopt;
    }

    MotionSpec<QRectF> spec;
    spec.profile = params.profile; // full copy — every field propagates
    spec.clock = clock;
    spec.retargetPolicy = params.retargetPolicy;
    return spec;
}

} // namespace SnapPolicy
} // namespace PhosphorAnimation
