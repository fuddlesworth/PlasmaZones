// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Geometric query helpers for AnimatedValue<T, Space>. Included from
// AnimatedValue.h — do not include directly.

namespace PhosphorAnimation {

/// Bounding rectangle of the swept path including curve overshoot.
template<typename T, ColorSpace Space>
QRectF AnimatedValue<T, Space>::bounds() const
    requires detail::PositionalGeometric<T>
{
    return boundsImpl();
}

/// Damage rect anchored at @p anchor — QSizeF specialisation only.
template<typename T, ColorSpace Space>
QRectF AnimatedValue<T, Space>::boundsAt(QPointF anchor) const
    requires detail::SizeGeometric<T>
{
    return QRectF(anchor, sweptSizeImpl().second);
}

/// (minSize, maxSize) the animation sweeps through, including overshoot.
template<typename T, ColorSpace Space>
std::pair<QSizeF, QSizeF> AnimatedValue<T, Space>::sweptSize() const
    requires detail::SizeGeometric<T>
{
    return sweptSizeImpl();
}

/// True when current size diverges from target by > @p epsilonPx on either axis.
template<typename T, ColorSpace Space>
bool AnimatedValue<T, Space>::hasSizeChange(qreal epsilonPx) const
    requires std::same_as<T, QRectF>
{
    return qAbs(m_current.width() - m_to.width()) > epsilonPx || qAbs(m_current.height() - m_to.height()) > epsilonPx;
}

/// (lo, hi) of a scalar animation's swept range, with overshoot.
template<typename T, ColorSpace Space>
std::pair<T, T> AnimatedValue<T, Space>::sweptRange() const
    requires detail::ScalarValue<T>
{
    return sweptRangeImpl();
}

// ─── Private impl helpers ───

// Uses explicit min/max reduction instead of QRectF::united() because
// Qt treats zero-width/zero-height rects as "empty" and drops them.
template<typename T, ColorSpace Space>
QRectF AnimatedValue<T, Space>::boundsImpl() const
    requires detail::PositionalGeometric<T>
{
    qreal minX, minY, maxX, maxY;
    if constexpr (std::same_as<T, QPointF>) {
        minX = std::min(m_from.x(), m_to.x());
        maxX = std::max(m_from.x(), m_to.x());
        minY = std::min(m_from.y(), m_to.y());
        maxY = std::max(m_from.y(), m_to.y());
        sampleOvershoots(minX, minY, maxX, maxY, [this](qreal p) {
            const QPointF s = Interpolate<QPointF>::lerp(m_from, m_to, p);
            return std::tuple<qreal, qreal, qreal, qreal>{s.x(), s.y(), s.x(), s.y()};
        });
    } else {
        minX = std::min(m_from.left(), m_to.left());
        minY = std::min(m_from.top(), m_to.top());
        maxX = std::max(m_from.right(), m_to.right());
        maxY = std::max(m_from.bottom(), m_to.bottom());
        sampleOvershoots(minX, minY, maxX, maxY, [this](qreal p) {
            const QRectF s = Interpolate<QRectF>::lerp(m_from, m_to, p);
            return std::tuple<qreal, qreal, qreal, qreal>{s.left(), s.top(), s.right(), s.bottom()};
        });
    }
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

template<typename T, ColorSpace Space>
std::pair<QSizeF, QSizeF> AnimatedValue<T, Space>::sweptSizeImpl() const
    requires detail::SizeGeometric<T>
{
    qreal minW = std::min(m_from.width(), m_to.width());
    qreal maxW = std::max(m_from.width(), m_to.width());
    qreal minH = std::min(m_from.height(), m_to.height());
    qreal maxH = std::max(m_from.height(), m_to.height());
    const auto curve = effectiveCurve();
    if (curve && curve->overshoots()) {
        for (int i = 1; i < kOvershootSamples; ++i) {
            // Same envelope the lerp applies, so the swept bounds describe the
            // geometry that will actually be drawn rather than an unbounded
            // excursion the renderer would never produce.
            const qreal p = boundCurveProgress(curve->evaluate(qreal(i) / kOvershootSamples));
            const QSizeF sampled = Interpolate<QSizeF>::lerp(m_from, m_to, p);
            minW = std::min(minW, sampled.width());
            maxW = std::max(maxW, sampled.width());
            minH = std::min(minH, sampled.height());
            maxH = std::max(maxH, sampled.height());
        }
    }
    return {QSizeF(minW, minH), QSizeF(maxW, maxH)};
}

template<typename T, ColorSpace Space>
template<typename Sampler>
void AnimatedValue<T, Space>::sampleOvershoots(qreal& minX, qreal& minY, qreal& maxX, qreal& maxY,
                                               const Sampler& sampleAt) const
{
    const auto curve = effectiveCurve();
    if (!curve || !curve->overshoots()) {
        return;
    }
    for (int i = 1; i < kOvershootSamples; ++i) {
        const qreal p = boundCurveProgress(curve->evaluate(qreal(i) / kOvershootSamples));
        const auto [x1, y1, x2, y2] = sampleAt(p);
        minX = std::min(minX, x1);
        minY = std::min(minY, y1);
        maxX = std::max(maxX, x2);
        maxY = std::max(maxY, y2);
    }
}

template<typename T, ColorSpace Space>
std::pair<T, T> AnimatedValue<T, Space>::sweptRangeImpl() const
{
    T lo = std::min(m_from, m_to);
    T hi = std::max(m_from, m_to);
    const auto curve = effectiveCurve();
    if (curve && curve->overshoots()) {
        for (int i = 1; i < kOvershootSamples; ++i) {
            // Same envelope the lerp applies, so the swept range describes the
            // values that will actually be produced.
            const qreal p = boundCurveProgress(curve->evaluate(qreal(i) / kOvershootSamples));
            const T sampled = Interpolate<T>::lerp(m_from, m_to, p);
            lo = std::min(lo, sampled);
            hi = std::max(hi, sampled);
        }
    }
    return {lo, hi};
}

} // namespace PhosphorAnimation
