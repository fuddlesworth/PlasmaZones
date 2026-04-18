// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationMath.h>

namespace PhosphorAnimation {
namespace AnimationMath {

std::optional<WindowMotion> createSnapMotion(const QPointF& oldPosition, const QSizeF& oldSize,
                                             const QRect& targetGeometry, qreal duration, const Easing& easing,
                                             int minDistance)
{
    if (targetGeometry.width() <= 0 || targetGeometry.height() <= 0) {
        return std::nullopt;
    }

    const QPointF newPos = targetGeometry.topLeft();
    const qreal dist = QLineF(oldPosition, newPos).length();
    const bool sizeChanging =
        qAbs(oldSize.width() - targetGeometry.width()) > 1.0 || qAbs(oldSize.height() - targetGeometry.height()) > 1.0;
    if (dist < qMax(1.0, qreal(minDistance)) && !sizeChanging) {
        return std::nullopt;
    }

    WindowMotion motion;
    motion.startPosition = oldPosition;
    motion.startSize = oldSize;
    motion.targetGeometry = targetGeometry;
    motion.duration = duration;
    motion.easing = easing;
    return motion;
}

QRectF repaintBounds(const QPointF& startPos, const QSizeF& startSize, const QRect& targetGeometry,
                     const Easing& easing, const QMarginsF& padding)
{
    QRectF atTarget(targetGeometry.x() - padding.left(), targetGeometry.y() - padding.top(),
                    targetGeometry.width() + padding.left() + padding.right(),
                    targetGeometry.height() + padding.top() + padding.bottom());
    QRectF atStart(startPos.x() - padding.left(), startPos.y() - padding.top(),
                   startSize.width() + padding.left() + padding.right(),
                   startSize.height() + padding.top() + padding.bottom());

    QRectF bounds = atTarget.united(atStart);

    // Curves that overshoot the unit interval need sampling: otherwise
    // we'd under-invalidate mid-animation and leave painting artifacts.
    const bool isBounce = (easing.type == Easing::Type::BounceIn || easing.type == Easing::Type::BounceOut
                           || easing.type == Easing::Type::BounceInOut);
    const bool needsSampling = (easing.type == Easing::Type::ElasticIn || easing.type == Easing::Type::ElasticOut
                                || easing.type == Easing::Type::ElasticInOut)
        || (isBounce && easing.amplitude > 1.0)
        || (easing.type == Easing::Type::CubicBezier
            && (easing.y1 < 0.0 || easing.y1 > 1.0 || easing.y2 < 0.0 || easing.y2 > 1.0));

    if (needsSampling) {
        const qreal dx = startPos.x() - targetGeometry.x();
        const qreal dy = startPos.y() - targetGeometry.y();
        const qreal dw = startSize.width() - targetGeometry.width();
        const qreal dh = startSize.height() - targetGeometry.height();

        // 50 samples ≈ one sample every 20 ms at the typical 1000 ms
        // upper bound on snap durations — fine for the parameter ranges
        // these curves clamp to (elastic amplitude ≤ 3, bounce count ≤ 8,
        // bezier y ∈ [-1, 2]). Bounce-out with N=8 still gets ~6 samples
        // per sub-bounce, enough to capture each peak. Longer / wilder
        // curves should drive their own per-frame repaint instead.
        constexpr int nSamples = 50;
        for (int i = 1; i < nSamples; ++i) {
            qreal p = easing.evaluate(qreal(i) / nSamples);
            const qreal inv = 1.0 - p;
            QRectF sampledRect(targetGeometry.x() + dx * inv - padding.left(),
                               targetGeometry.y() + dy * inv - padding.top(),
                               targetGeometry.width() + dw * inv + padding.left() + padding.right(),
                               targetGeometry.height() + dh * inv + padding.top() + padding.bottom());
            bounds = bounds.united(sampledRect);
        }
    }

    // 2 px slack for subpixel damage that QRectF union alone misses.
    bounds.adjust(-2.0, -2.0, 2.0, 2.0);
    return bounds;
}

} // namespace AnimationMath
} // namespace PhosphorAnimation
