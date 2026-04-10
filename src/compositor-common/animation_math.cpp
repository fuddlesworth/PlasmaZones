// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animation_math.h"

namespace PlasmaZones {
namespace AnimationMath {

std::optional<WindowAnimation> createSnapAnimation(const QPointF& oldPosition, const QSizeF& oldSize,
                                                   const QRect& targetGeometry, const AnimationConfig& config)
{
    if (!config.enabled) {
        return std::nullopt;
    }

    if (targetGeometry.width() <= 0 || targetGeometry.height() <= 0) {
        return std::nullopt;
    }

    const QPointF newPos = targetGeometry.topLeft();
    const qreal dist = QLineF(oldPosition, newPos).length();
    const bool sizeChanging =
        qAbs(oldSize.width() - targetGeometry.width()) > 1.0 || qAbs(oldSize.height() - targetGeometry.height()) > 1.0;
    if (dist < qMax(1.0, qreal(config.minDistance)) && !sizeChanging) {
        return std::nullopt;
    }

    WindowAnimation anim;
    anim.startPosition = oldPosition;
    anim.startSize = oldSize;
    anim.targetGeometry = targetGeometry;
    anim.duration = config.duration;
    anim.easing = config.easing;
    return anim;
}

QRectF computeOvershootBounds(const QPointF& startPos, const QSizeF& startSize, const QRect& targetGeometry,
                              const EasingCurve& easing, const QMarginsF& padding)
{
    QRectF atTarget(targetGeometry.x() - padding.left(), targetGeometry.y() - padding.top(),
                    targetGeometry.width() + padding.left() + padding.right(),
                    targetGeometry.height() + padding.top() + padding.bottom());
    QRectF atStart(startPos.x() - padding.left(), startPos.y() - padding.top(),
                   startSize.width() + padding.left() + padding.right(),
                   startSize.height() + padding.top() + padding.bottom());

    QRectF bounds = atTarget.united(atStart);

    const bool isBounce = (easing.type == EasingCurve::Type::BounceIn || easing.type == EasingCurve::Type::BounceOut
                           || easing.type == EasingCurve::Type::BounceInOut);
    const bool needsSampling =
        (easing.type == EasingCurve::Type::ElasticIn || easing.type == EasingCurve::Type::ElasticOut
         || easing.type == EasingCurve::Type::ElasticInOut)
        || (isBounce && easing.amplitude > 1.0)
        || (easing.type == EasingCurve::Type::CubicBezier
            && (easing.y1 < 0.0 || easing.y1 > 1.0 || easing.y2 < 0.0 || easing.y2 > 1.0));

    if (needsSampling) {
        const qreal dx = startPos.x() - targetGeometry.x();
        const qreal dy = startPos.y() - targetGeometry.y();
        const qreal dw = startSize.width() - targetGeometry.width();
        const qreal dh = startSize.height() - targetGeometry.height();

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

    bounds.adjust(-2.0, -2.0, 2.0, 2.0);
    return bounds;
}

} // namespace AnimationMath
} // namespace PlasmaZones
