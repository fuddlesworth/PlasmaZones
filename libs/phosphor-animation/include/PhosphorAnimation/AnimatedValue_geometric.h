// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Geometric query helpers for AnimatedValue<T, Space>.
//
// This file is a continuation of <PhosphorAnimation/AnimatedValue.h> — it
// is included unconditionally from the bottom of that header so every
// existing consumer (`#include <PhosphorAnimation/AnimatedValue.h>`)
// transparently sees the full template surface. There is no need (and no
// supported path) for downstream code to include this file directly —
// referencing only `<PhosphorAnimation/AnimatedValue.h>` keeps the public
// dependency contract one-header-wide.
//
// What lives here:
//   - Public geometric query API: `bounds()`, `boundsAt()`, `sweptSize()`,
//     `hasSizeChange()`, `sweptRange()`. Each is constrained by a `requires`
//     clause matching its concept (`PositionalGeometric`, `SizeGeometric`,
//     `ScalarValue`, or `same_as<QRectF>`) so out-of-class definitions
//     mirror the in-class declarations exactly.
//   - Private impl helpers: `boundsImpl`, `sweptSizeImpl`, `sampleOvershoots`,
//     `sweptRangeImpl`. These exist so the public surface routes through
//     constraint-free internal code paths (the constraint is enforced at
//     the public entry; the impls assume a satisfying T).
//   - The `kOvershootSamples` static is defined inline in
//     `AnimatedValue.h` since it is consumed exclusively by the helpers
//     in this file but the class body needs the declaration.
//
// Why this split exists:
//   The combined header crossed the project's 800-line cap. The geometric
//   surface is logically separable (no caller needs `bounds()` without
//   `start()`, but the inverse is common — the snap engine never queries
//   bounds on its own animations) and each helper is a self-contained
//   reduction over endpoints + curve overshoot samples, so cohesion within
//   the file is preserved after the cut.
//
// Header-only template definitions: every method is a member-template of
// `AnimatedValue<T, Space>`, instantiated on demand at the call site.
// ABI-compatible with the pre-split layout because the class body shape
// (member ordering, sizes, virtual table) is unchanged.

namespace PhosphorAnimation {

// ─── Public geometric API ────────────────────────────────────────────────────

/**
 * @brief Bounding rectangle covering the full animation path
 *        including curve overshoot — positional specialisations.
 *
 * Union of the start and target values (treated as positions or
 * rects); for curves where `overshoots()` is true, the curve is
 * additionally sampled at 50 points and each sample union'd in.
 * The result is the damage rect the consumer must invalidate so
 * no frame of the animation paints outside an already-invalidated
 * region.
 *
 * Available only on specialisations that carry a position:
 * `QPointF` (bounding box of the two endpoint positions) and
 * `QRectF` (union of the two endpoint rects). `QSizeF` has no
 * inherent position — use `boundsAt(anchor)` or `sweptSize()`
 * instead. Scalar / colour / transform specialisations expose
 * `sweptRange()` or leave damage to the owning item (see design
 * doc decision E).
 */
template<typename T, ColorSpace Space>
QRectF AnimatedValue<T, Space>::bounds() const
    requires detail::PositionalGeometric<T>
{
    return boundsImpl();
}

/**
 * @brief Bounding rectangle anchored at @p anchor — size-only T.
 *
 * Computes the envelope of widths and heights swept during the
 * animation (including overshoot samples) and returns a rect
 * rooted at @p anchor with those dimensions. The caller supplies
 * the anchor because a `QSizeF` animation does not carry
 * positional information — forcing the anchor through the API
 * makes the precondition explicit and eliminates the silent
 * origin-at-(0, 0) trap a combined `bounds()` signature would
 * otherwise hide.
 *
 * Available only on the `QSizeF` specialisation.
 */
template<typename T, ColorSpace Space>
QRectF AnimatedValue<T, Space>::boundsAt(QPointF anchor) const
    requires detail::SizeGeometric<T>
{
    // Damage envelope anchored at @p anchor is the MAX dimension
    // observed over the swept path — a rect smaller than `hiSize`
    // would be contained within the damage rect that `hiSize`
    // produces. `sweptSize()` exposes both (lo, hi) for consumers
    // doing explicit layout math; damage tracking only needs hi.
    // Only the `hi` bound is needed for the damage rect; the `lo`
    // bound is part of `sweptSizeImpl`'s pair return for callers
    // of `sweptSize()`. Pick out the second element directly so
    // no unused-binding discard is needed.
    return QRectF(anchor, sweptSizeImpl().second);
}

/**
 * @brief Min / max size the animation sweeps through, including
 *        curve overshoot.
 *
 * Componentwise analogue of `sweptRange()` for scalar T. Returns
 * `(minSize, maxSize)` where each field is the min/max of that
 * dimension observed across the endpoints and any overshoot
 * samples. Consumers doing their own anchor / layout math use
 * this instead of `boundsAt` when the anchor isn't known ahead
 * of time.
 *
 * Available only on the `QSizeF` specialisation.
 */
template<typename T, ColorSpace Space>
std::pair<QSizeF, QSizeF> AnimatedValue<T, Space>::sweptSize() const
    requires detail::SizeGeometric<T>
{
    return sweptSizeImpl();
}

/**
 * @brief Has the current interpolated value diverged from the
 *        target size by more than @p epsilonPx on either axis?
 *
 * Intended for compositor adapters that only need to apply a
 * scale transform when size is actually changing (pure-translate
 * snaps leave scale at identity). Replaces the hand-inlined
 * "|current.w - target.w| > 1.0 || ..." check that otherwise
 * duplicates across every adapter.
 *
 * Default `epsilonPx` matches `SnapPolicy::kSnapSizeEpsilonPx` —
 * the snap gate and the paint-path scale decision agree on what
 * counts as "size changed", so a sub-pixel delta cannot flip one
 * side without flipping the other.
 *
 * Available only on the `QRectF` specialisation — `QSizeF` has
 * no natural "target" distinct from the lerp endpoint (every
 * animation of a size changes the size), and geometric `QPointF`
 * has no size axis at all.
 */
template<typename T, ColorSpace Space>
bool AnimatedValue<T, Space>::hasSizeChange(qreal epsilonPx) const
    requires std::same_as<T, QRectF>
{
    return qAbs(m_current.width() - m_to.width()) > epsilonPx || qAbs(m_current.height() - m_to.height()) > epsilonPx;
}

/**
 * @brief Min / max value the animation sweeps through, including
 *        curve overshoot.
 *
 * Available only on scalar specialisations. Consumers use this
 * for damage tracking or axis range sizing where "bounds as a
 * rect" doesn't make sense (a scalar opacity's swept range is
 * [0.0, 1.0] or with overshoot [-0.02, 1.05]).
 */
template<typename T, ColorSpace Space>
std::pair<T, T> AnimatedValue<T, Space>::sweptRange() const
    requires detail::ScalarValue<T>
{
    return sweptRangeImpl();
}

// ─── Private impl helpers ────────────────────────────────────────────────────

// Positional bounds — position-carrying T only (QPointF, QRectF).
// QSizeF has its own sweptSizeImpl below; the public API routes
// QSizeF callers through boundsAt(anchor) / sweptSize() so the
// anchor precondition is explicit at the call site.
//
// Implemented via explicit (minX, minY, maxX, maxY) reduction
// instead of `QRectF::united()` because Qt treats zero-width /
// zero-height rects as "empty" and drops them from a union — a
// QPointF sweep (effectively a degenerate rect at each endpoint)
// would collapse to a single endpoint if run through united().
// min/max over the endpoint-and-sample set is the robust formulation.
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
        // QRectF — full swept rect including overshoot.
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

// Size-envelope implementation — QSizeF only. Returns the min/max
// width and height observed across the two endpoints plus any
// overshoot samples. The public surface wraps this with either an
// explicit anchor (`boundsAt`) or raw endpoint-pair access
// (`sweptSize`) so the caller controls the coordinate mapping.
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
            const qreal p = curve->evaluate(qreal(i) / kOvershootSamples);
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
        const qreal p = curve->evaluate(qreal(i) / kOvershootSamples);
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
            const qreal p = curve->evaluate(qreal(i) / kOvershootSamples);
            const T sampled = Interpolate<T>::lerp(m_from, m_to, p);
            lo = std::min(lo, sampled);
            hi = std::max(hi, sampled);
        }
    }
    return {lo, hi};
}

} // namespace PhosphorAnimation
