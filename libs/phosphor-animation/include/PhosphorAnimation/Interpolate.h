// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QColor>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <QtGlobal>
#include <QtMath>

#include <cmath>
#include <concepts>
#include <type_traits>

namespace PhosphorAnimation {

/**
 * @brief Canonical sub-pixel epsilon for rect size-change detection.
 *
 * Used by `AnimatedValue<QRectF>::hasSizeChange` and re-exported as
 * `SnapPolicy::kSnapSizeEpsilonPx` so the snap gate and the paint-path
 * scale decision agree on what counts as "size changed". 1.0 px is
 * the smallest value that still rejects sub-pixel noise — anything
 * smaller would flip under rounding even when the layout is stable.
 *
 * Lives on the generic AnimatedValue layer (not SnapPolicy) because
 * the primitive is the natural owner: any consumer of
 * `AnimatedValue<QRectF>` needs this epsilon; SnapPolicy is one such
 * consumer among others.
 */
inline constexpr qreal kRectSizeEpsilonPx = 1.0;

/**
 * @brief Interpolation space selector for `AnimatedValue<QColor, …>`.
 *
 * - `Linear` (default): lerp in linear-space RGB. sRGB → linear on
 *   each conversion, linear lerp, linear → sRGB on value(). Matches
 *   the blending behaviour of every compositor shader pipeline;
 *   midpoint of complementary colours is chromatic, not grey.
 * - `OkLab`: lerp in OkLab perceptually uniform space. Costs a
 *   second matrix transform per conversion but produces midpoints
 *   that match perceptual chromatic gradients (useful for colour
 *   pickers / gradient strips where hue-shift matters more than
 *   radiometric correctness).
 *
 * The parameter is ignored by every non-`QColor` specialisation of
 * `AnimatedValue<T, Space>`.
 */
enum class ColorSpace {
    Linear,
    OkLab,
};

// ═══════════════════════════════════════════════════════════════════════════════
// Interpolate<T> — per-type lerp + distance helpers
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Type-specific linear interpolation and path-distance for
 *        `AnimatedValue<T>`.
 *
 * `AnimatedValue<T>` progresses a *scalar* normalized parameter in [0, 1]
 * (the curve's `evaluate()` / `step()` output) and uses this helper to
 * lerp concrete T-values along the start→target segment. Specialisations
 * shipped in this header cover `qreal`, `QPointF`, `QSizeF`, `QRectF`,
 * `QColor` (linear-space + OkLab opt-in) and `QTransform` (polar-
 * decomposed).
 *
 * `distance` is used by the `PreserveVelocity` retarget path — world-
 * space rate is scalar-by-magnitude, so vector specialisations compute
 * Euclidean magnitude. Direction is not preserved across retarget for
 * vector T (see Phase 3 decision B / design doc note on retarget
 * semantics).
 */
template<typename T>
struct Interpolate;

template<>
struct Interpolate<qreal>
{
    static qreal lerp(qreal from, qreal to, qreal t)
    {
        return from + (to - from) * t;
    }
    static qreal distance(qreal from, qreal to)
    {
        return qAbs(to - from);
    }
};

template<>
struct Interpolate<QPointF>
{
    static QPointF lerp(const QPointF& from, const QPointF& to, qreal t)
    {
        return QPointF(from.x() + (to.x() - from.x()) * t, from.y() + (to.y() - from.y()) * t);
    }
    static qreal distance(const QPointF& from, const QPointF& to)
    {
        return QLineF(from, to).length();
    }
};

template<>
struct Interpolate<QSizeF>
{
    static QSizeF lerp(const QSizeF& from, const QSizeF& to, qreal t)
    {
        return QSizeF(from.width() + (to.width() - from.width()) * t,
                      from.height() + (to.height() - from.height()) * t);
    }
    static qreal distance(const QSizeF& from, const QSizeF& to)
    {
        const qreal dw = to.width() - from.width();
        const qreal dh = to.height() - from.height();
        return std::sqrt(dw * dw + dh * dh);
    }
};

template<>
struct Interpolate<QRectF>
{
    static QRectF lerp(const QRectF& from, const QRectF& to, qreal t)
    {
        return QRectF(Interpolate<QPointF>::lerp(from.topLeft(), to.topLeft(), t),
                      Interpolate<QSizeF>::lerp(from.size(), to.size(), t));
    }
    static qreal distance(const QRectF& from, const QRectF& to)
    {
        // 4-D Euclidean norm over (x, y, w, h) — matches the behaviour
        // the snap-animation path in Phase 2 implicitly used when it
        // combined position and size into a single animation progress.
        //
        // ## Metric caveat
        //
        // The norm conflates position-pixels and size-pixels: a 1000 px
        // translate and a 1000 px resize return the same distance, so
        // the `PreserveVelocity` retarget rescale treats them as
        // equivalent "rates of change". For the snap use case this is
        // fine — window snap is position-dominated and size deltas are
        // small relative to translates. Generic consumers of
        // `AnimatedValue<QRectF>` whose animations are size-dominated
        // (grid cell resize, expanding tooltip) may observe a weird
        // velocity rescale on retarget; prefer `PreservePosition` or
        // `ResetVelocity` in that case, or split position and size
        // into two separate `AnimatedValue` instances whose metrics
        // are pure.
        const qreal dp = Interpolate<QPointF>::distance(from.topLeft(), to.topLeft());
        const qreal ds = Interpolate<QSizeF>::distance(from.size(), to.size());
        return std::sqrt(dp * dp + ds * ds);
    }
};

// ─── QColor linear-space RGB interpolation ───
//
// The default `Interpolate<QColor>` path. `AnimatedValue<QColor>` with
// the default `ColorSpace::Linear` dispatches through this. For OkLab
// opt-in via `AnimatedValue<QColor, ColorSpace::OkLab>`, the template
// bypasses this and calls `detail::lerpColorOkLab` directly — see the
// `advance()` dispatch inside AnimatedValue.

namespace detail {

// sRGB ↔ linear — the IEC 61966-2-1 piecewise definition.
// `qPow` is Qt's `std::pow` alias; the 2.4 exponent here is the
// canonical sRGB gamma (not 2.2; 2.2 is a crude approximation).
inline qreal srgbToLinear(qreal c)
{
    return c <= 0.04045 ? c / 12.92 : qPow((c + 0.055) / 1.055, 2.4);
}
inline qreal linearToSrgb(qreal c)
{
    return c <= 0.0031308 ? c * 12.92 : 1.055 * qPow(c, 1.0 / 2.4) - 0.055;
}

inline QColor lerpColorLinear(const QColor& from, const QColor& to, qreal t)
{
    const qreal fR = srgbToLinear(from.redF());
    const qreal fG = srgbToLinear(from.greenF());
    const qreal fB = srgbToLinear(from.blueF());
    const qreal tR = srgbToLinear(to.redF());
    const qreal tG = srgbToLinear(to.greenF());
    const qreal tB = srgbToLinear(to.blueF());

    const qreal rLin = fR + (tR - fR) * t;
    const qreal gLin = fG + (tG - fG) * t;
    const qreal bLin = fB + (tB - fB) * t;

    // Alpha is gamma-independent.
    const qreal a = from.alphaF() + (to.alphaF() - from.alphaF()) * t;

    QColor result;
    result.setRgbF(qBound(0.0, linearToSrgb(rLin), 1.0), qBound(0.0, linearToSrgb(gLin), 1.0),
                   qBound(0.0, linearToSrgb(bLin), 1.0), qBound(0.0, a, 1.0));
    return result;
}

// OkLab — Björn Ottosson's perceptually-uniform lab space.
// Reference: https://bottosson.github.io/posts/oklab/
//
// Conversion: sRGB → linear RGB → LMS → cube root → OkLab.
// The LMS→OkLab matrix below is the published OkLab M2 matrix.

struct OkLab
{
    qreal L, a, b;
};

inline OkLab linearToOkLab(qreal r, qreal g, qreal bv)
{
    const qreal l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * bv;
    const qreal m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * bv;
    const qreal s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * bv;

    const qreal lCbrt = std::cbrt(l);
    const qreal mCbrt = std::cbrt(m);
    const qreal sCbrt = std::cbrt(s);

    OkLab out;
    out.L = 0.2104542553 * lCbrt + 0.7936177850 * mCbrt - 0.0040720468 * sCbrt;
    out.a = 1.9779984951 * lCbrt - 2.4285922050 * mCbrt + 0.4505937099 * sCbrt;
    out.b = 0.0259040371 * lCbrt + 0.7827717662 * mCbrt - 0.8086757660 * sCbrt;
    return out;
}

inline void okLabToLinear(const OkLab& lab, qreal& r, qreal& g, qreal& bv)
{
    const qreal lCbrt = lab.L + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
    const qreal mCbrt = lab.L - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
    const qreal sCbrt = lab.L - 0.0894841775 * lab.a - 1.2914855480 * lab.b;

    const qreal l = lCbrt * lCbrt * lCbrt;
    const qreal m = mCbrt * mCbrt * mCbrt;
    const qreal s = sCbrt * sCbrt * sCbrt;

    r = +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    bv = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
}

inline QColor lerpColorOkLab(const QColor& from, const QColor& to, qreal t)
{
    const qreal fR = srgbToLinear(from.redF());
    const qreal fG = srgbToLinear(from.greenF());
    const qreal fB = srgbToLinear(from.blueF());
    const qreal tR = srgbToLinear(to.redF());
    const qreal tG = srgbToLinear(to.greenF());
    const qreal tB = srgbToLinear(to.blueF());

    const OkLab fLab = linearToOkLab(fR, fG, fB);
    const OkLab tLab = linearToOkLab(tR, tG, tB);

    OkLab mid;
    mid.L = fLab.L + (tLab.L - fLab.L) * t;
    mid.a = fLab.a + (tLab.a - fLab.a) * t;
    mid.b = fLab.b + (tLab.b - fLab.b) * t;

    qreal rLin, gLin, bLin;
    okLabToLinear(mid, rLin, gLin, bLin);

    const qreal a = from.alphaF() + (to.alphaF() - from.alphaF()) * t;

    QColor result;
    result.setRgbF(qBound(0.0, linearToSrgb(rLin), 1.0), qBound(0.0, linearToSrgb(gLin), 1.0),
                   qBound(0.0, linearToSrgb(bLin), 1.0), qBound(0.0, a, 1.0));
    return result;
}

inline qreal colorDistance(const QColor& from, const QColor& to)
{
    // L2 norm in linear-RGB space — matches the space the default
    // Interpolate<QColor>::lerp operates in, so the velocity rescale
    // at retarget uses a metric consistent with the progression itself.
    // sRGB-space distance would have worked "approximately" (the ratio
    // is what drives the rescale) but picking the same space as the
    // lerp keeps the math principled.
    const qreal dR = srgbToLinear(to.redF()) - srgbToLinear(from.redF());
    const qreal dG = srgbToLinear(to.greenF()) - srgbToLinear(from.greenF());
    const qreal dB = srgbToLinear(to.blueF()) - srgbToLinear(from.blueF());
    const qreal dA = to.alphaF() - from.alphaF(); // alpha is gamma-independent
    return std::sqrt(dR * dR + dG * dG + dB * dB + dA * dA);
}

} // namespace detail

template<>
struct Interpolate<QColor>
{
    /**
     * @brief Linear-space sRGB lerp.
     *
     * **Always Linear-space.** The `ColorSpace::OkLab` opt-in is a
     * property of `AnimatedValue<QColor, ColorSpace::OkLab>` — the
     * template dispatches through `AnimatedValue::lerpStateValue()`
     * which routes to `detail::lerpColorOkLab` directly for that
     * instantiation. The free-standing `Interpolate<QColor>::lerp`
     * does NOT honour the `Space` parameter and is unconditionally
     * Linear; direct callers (non-`AnimatedValue`) who want OkLab
     * must call `detail::lerpColorOkLab` themselves.
     */
    static QColor lerp(const QColor& from, const QColor& to, qreal t)
    {
        return detail::lerpColorLinear(from, to, t);
    }
    /// Linear-space L2 distance — matches the lerp space for the default
    /// (Linear) path. An OkLab `AnimatedValue<QColor>` still uses this
    /// metric for retarget velocity rescale; the OkLab perceptual
    /// "distance" is different but the rescale ratio
    /// (newVel = oldVel × oldDist / newDist) is close enough when both
    /// distances use the same metric.
    static qreal distance(const QColor& from, const QColor& to)
    {
        return detail::colorDistance(from, to);
    }
};

// ─── QTransform polar-decomposed interpolation ───
//
// Component-wise lerp of 3×3 affine matrices shears visibly during
// rotation (a rotate(0°)→rotate(90°) interpolated component-wise
// passes through a squashed matrix at t=0.5, not rotate(45°)).
// Correct answer: decompose into translate + rotate + scale + shear,
// interpolate each component independently, recompose.

namespace detail {

struct DecomposedTransform
{
    qreal tx = 0.0, ty = 0.0;
    qreal rotation = 0.0; // radians; shortest-arc slerp during lerp
    qreal sx = 1.0, sy = 1.0;
    qreal shear = 0.0;
};

inline DecomposedTransform decomposeTransform(const QTransform& t)
{
    DecomposedTransform d;
    d.tx = t.dx();
    d.ty = t.dy();

    const qreal m11 = t.m11();
    const qreal m12 = t.m12();
    const qreal m21 = t.m21();
    const qreal m22 = t.m22();

    // QR-style decomposition on the 2x2 linear part under Qt's
    // post-multiply row-vector convention:
    //   points' = points * M
    //   M = R × K × S where
    //     R = [[cos, sin], [-sin, cos]]          (Qt rotate(θ))
    //     K = [[1, shear], [0, 1]]               (x-shear)
    //     S = [[sx, 0], [0, sy]]
    //   → m11 = sx·cos
    //     m12 = sy·(shear·cos + sin)
    //     m21 = -sx·sin
    //     m22 = sy·(cos - shear·sin)
    //
    // Extract sx and θ from the first column (m11, m21), then
    // un-rotate the second column to split shear × sy from sy.
    //
    // Reflection handling: a matrix with negative determinant
    // (e.g. `QTransform::fromScale(-1, 1)`) is captured as
    // `sy` negative — the sy extraction below produces the correct
    // sign. The decomposition round-trips endpoint values faithfully.
    // However, *interpolation* between a positive-det and negative-det
    // transform is inherently discontinuous (see `lerpTransform`'s
    // reflection comment) — the polar path is used only when both
    // endpoints share determinant sign.
    d.sx = std::sqrt(m11 * m11 + m21 * m21);
    d.rotation = d.sx > 1.0e-9 ? std::atan2(-m21, m11) : 0.0;

    const qreal cosR = std::cos(d.rotation);
    const qreal sinR = std::sin(d.rotation);

    // Invert the (R × K × S) second-column decomposition analytically:
    //   sy          = m22·cos + m12·sin
    //   shear × sy  = m12·cos - m22·sin
    d.sy = m22 * cosR + m12 * sinR;
    const qreal shearTimesSy = m12 * cosR - m22 * sinR;
    d.shear = qAbs(d.sy) > 1.0e-9 ? shearTimesSy / d.sy : 0.0;
    return d;
}

inline QTransform recomposeTransform(const DecomposedTransform& d)
{
    // Recompose M = R × K × S under Qt's post-multiply convention
    // (see decomposeTransform above for the forward derivation).
    const qreal cosR = std::cos(d.rotation);
    const qreal sinR = std::sin(d.rotation);

    const qreal m11 = d.sx * cosR;
    const qreal m12 = d.sy * (d.shear * cosR + sinR);
    const qreal m21 = -d.sx * sinR;
    const qreal m22 = d.sy * (cosR - d.shear * sinR);

    return QTransform(m11, m12, m21, m22, d.tx, d.ty);
}

/**
 * @brief Is @p t a pure-translate transform (identity linear part)?
 *
 * Used by `AnimatedValue<QTransform>::retarget` to detect the one case
 * where `PreserveVelocity` has physically-meaningful semantics — the
 * Frobenius metric collapses to Euclidean distance on (dx, dy) when
 * the 2×2 linear part is the identity on both segments.
 */
inline bool isPureTranslate(const QTransform& t)
{
    constexpr qreal kIdentityEps = 1.0e-9;
    return qAbs(t.m11() - 1.0) < kIdentityEps && qAbs(t.m22() - 1.0) < kIdentityEps && qAbs(t.m12()) < kIdentityEps
        && qAbs(t.m21()) < kIdentityEps;
}

inline QTransform lerpTransform(const QTransform& from, const QTransform& to, qreal t)
{
    // Short-circuit at segment endpoints: decompose/recompose is lossy
    // (~1 ulp per matrix element) so lerp(A, B, 0) would otherwise not
    // bit-exact-equal A. Every other Interpolate<T> specialisation is
    // exact at the endpoints — preserve that invariant here.
    if (t <= 0.0) {
        return from;
    }
    if (t >= 1.0) {
        return to;
    }

    // Reflection (determinant sign change) limitation:
    //
    // Polar decomposition into rotation + scale + shear cannot smoothly
    // interpolate between endpoints whose 2x2 linear parts have
    // determinants of opposite sign — any such interpolation MUST pass
    // through a singular matrix at some t, because det is continuous
    // along a smooth path. When we detect that case, fall back to a
    // component-wise lerp. Component-wise also produces non-rigid
    // intermediates (same failure mode that polar exists to avoid on
    // pure rotations), but for reflection endpoints there is no
    // rigid-body smooth path by construction — the library simply
    // cannot produce a meaningful interpolation and the caller should
    // split the animation into separate segments (to → identity →
    // reflected-to) if they need continuous motion.
    //
    // Near-singular endpoint gate: a matrix whose 2x2 linear part has
    // |det| below `kNearSingularDet` decomposes unreliably — `sx` goes
    // to sub-epsilon, the rotation extraction (`atan2(-m21, m11)`)
    // silently resolves to 0 because the guard at `decomposeTransform`
    // suppresses meaningful angles below `sx > 1e-9`, and the slerp
    // path then interpolates across the wrong angular delta producing
    // a visible jump. Route near-singular endpoints through the same
    // component-wise path as the reflection branch — the intermediate
    // isn't rigid, but every visual frame is well-defined and the
    // caller is warned (via the fallback's lossy-ness) that the motion
    // is degenerate by construction.
    constexpr qreal kNearSingularDet = 1.0e-6;
    const qreal detFrom = from.m11() * from.m22() - from.m12() * from.m21();
    const qreal detTo = to.m11() * to.m22() - to.m12() * to.m21();
    const bool reflection = detFrom * detTo < 0.0;
    const bool nearSingular = std::abs(detFrom) < kNearSingularDet || std::abs(detTo) < kNearSingularDet;
    if (reflection || nearSingular) {
        // Component-wise lerp via `Interpolate<qreal>::lerp` — the
        // canonical numerically-stable scalar form (any future change
        // to `Interpolate<qreal>::lerp` reaches every matrix entry
        // from this one call site without the lambda indirection).
        using S = Interpolate<qreal>;
        return QTransform(S::lerp(from.m11(), to.m11(), t), S::lerp(from.m12(), to.m12(), t),
                          S::lerp(from.m21(), to.m21(), t), S::lerp(from.m22(), to.m22(), t),
                          S::lerp(from.dx(), to.dx(), t), S::lerp(from.dy(), to.dy(), t));
    }

    const DecomposedTransform df = decomposeTransform(from);
    const DecomposedTransform dt = decomposeTransform(to);

    DecomposedTransform r;
    r.tx = df.tx + (dt.tx - df.tx) * t;
    r.ty = df.ty + (dt.ty - df.ty) * t;
    r.sx = df.sx + (dt.sx - df.sx) * t;
    r.sy = df.sy + (dt.sy - df.sy) * t;
    r.shear = df.shear + (dt.shear - df.shear) * t;

    // Shortest-arc slerp on rotation. Unwrap the delta into (-π, π]
    // so a 0→3π/2 rotation interpolates via the short way (0→-π/2)
    // rather than the long way (0→3π/2).
    qreal dRot = dt.rotation - df.rotation;
    while (dRot > M_PI) {
        dRot -= 2.0 * M_PI;
    }
    while (dRot < -M_PI) {
        dRot += 2.0 * M_PI;
    }
    r.rotation = df.rotation + dRot * t;

    return recomposeTransform(r);
}

} // namespace detail

template<>
struct Interpolate<QTransform>
{
    static QTransform lerp(const QTransform& from, const QTransform& to, qreal t)
    {
        return detail::lerpTransform(from, to, t);
    }
    static qreal distance(const QTransform& from, const QTransform& to)
    {
        // Frobenius norm of the matrix difference — includes translation
        // (pixel units) and the linear part (unitless / radian-ish). The
        // metric mixes units by construction: a unit-translate and a
        // unit-scale both contribute 1² to the total. This is adequate
        // for retarget velocity rescale on pure-translate transforms
        // (which is the overwhelmingly common case — window snap, scroll
        // offsets) but produces a physically meaningless scale for
        // mixed translate+rotate+scale retargets at speed. Such uses
        // should prefer PreservePosition / ResetVelocity retarget
        // policies until a unit-aware metric is designed.
        const qreal d11 = to.m11() - from.m11();
        const qreal d12 = to.m12() - from.m12();
        const qreal d21 = to.m21() - from.m21();
        const qreal d22 = to.m22() - from.m22();
        const qreal dtx = to.dx() - from.dx();
        const qreal dty = to.dy() - from.dy();
        return std::sqrt(d11 * d11 + d12 * d12 + d21 * d21 + d22 * d22 + dtx * dtx + dty * dty);
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Concept guards for AnimatedValue<T>::bounds() / sweptRange() members
// ═══════════════════════════════════════════════════════════════════════════════

namespace detail {

/**
 * @brief Geometric values that carry a position — their damage region
 *        is fully determined by the animation's endpoints + overshoot.
 *
 * Used to gate `bounds()`. A `QSizeF` animation has no inherent
 * position (only width/height), so it does NOT satisfy this concept
 * and must instead use `boundsAt(anchor)` or `sweptSize()`.
 */
template<typename T>
concept PositionalGeometric = std::same_as<T, QPointF> || std::same_as<T, QRectF>;

/**
 * @brief Geometric values that carry only a size — the caller must
 *        supply an anchor to turn a "swept size envelope" into a
 *        "damage rect in screen coordinates".
 */
template<typename T>
concept SizeGeometric = std::same_as<T, QSizeF>;

template<typename T>
concept ScalarValue = std::is_arithmetic_v<T>;

} // namespace detail

} // namespace PhosphorAnimation
