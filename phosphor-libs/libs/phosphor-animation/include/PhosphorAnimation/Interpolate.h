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

/// Sub-pixel epsilon for rect size-change detection. Shared between
/// AnimatedValue<QRectF>::hasSizeChange and SnapPolicy::kSnapSizeEpsilonPx.
inline constexpr qreal kRectSizeEpsilonPx = 1.0;

/// Interpolation space selector for AnimatedValue<QColor, ...>.
enum class ColorSpace {
    Linear, ///< sRGB -> linear lerp -> sRGB (radiometrically correct)
    OkLab, ///< sRGB -> OkLab lerp -> sRGB (perceptually uniform)
};

/// Type-specific linear interpolation and path-distance for AnimatedValue<T>.
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
    static constexpr qreal retargetEpsilon = 1.0e-6;
    static bool isFinite(qreal v)
    {
        return std::isfinite(v);
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
    static constexpr qreal retargetEpsilon = 0.5;
    static bool isFinite(const QPointF& p)
    {
        return std::isfinite(p.x()) && std::isfinite(p.y());
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
    static constexpr qreal retargetEpsilon = 0.5;
    static bool isFinite(const QSizeF& s)
    {
        return std::isfinite(s.width()) && std::isfinite(s.height());
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
    /// 4-D Euclidean norm over (x, y, w, h). Conflates position-pixels and
    /// size-pixels — fine for position-dominated snap; size-dominated consumers
    /// should use PreservePosition or split into separate AnimatedValues.
    static qreal distance(const QRectF& from, const QRectF& to)
    {
        const qreal dp = Interpolate<QPointF>::distance(from.topLeft(), to.topLeft());
        const qreal ds = Interpolate<QSizeF>::distance(from.size(), to.size());
        return std::sqrt(dp * dp + ds * ds);
    }
    static constexpr qreal retargetEpsilon = 0.5;
    static bool isFinite(const QRectF& r)
    {
        return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) && std::isfinite(r.height());
    }
};

namespace detail {

// sRGB <-> linear — IEC 61966-2-1 piecewise definition (2.4 exponent, not 2.2).
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
    const qreal a = from.alphaF() + (to.alphaF() - from.alphaF()) * t;

    QColor result;
    result.setRgbF(qBound(0.0, linearToSrgb(rLin), 1.0), qBound(0.0, linearToSrgb(gLin), 1.0),
                   qBound(0.0, linearToSrgb(bLin), 1.0), qBound(0.0, a, 1.0));
    return result;
}

// OkLab — Bjorn Ottosson's perceptually-uniform lab space.
// Conversion: sRGB -> linear RGB -> LMS -> cube root -> OkLab.

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

/// L2 norm in linear-RGB space — matches the lerp space so velocity
/// rescale uses a consistent metric.
inline qreal colorDistance(const QColor& from, const QColor& to)
{
    const qreal dR = srgbToLinear(to.redF()) - srgbToLinear(from.redF());
    const qreal dG = srgbToLinear(to.greenF()) - srgbToLinear(from.greenF());
    const qreal dB = srgbToLinear(to.blueF()) - srgbToLinear(from.blueF());
    const qreal dA = to.alphaF() - from.alphaF();
    return std::sqrt(dR * dR + dG * dG + dB * dB + dA * dA);
}

} // namespace detail

template<>
struct Interpolate<QColor>
{
    /// Linear-space sRGB lerp. OkLab is handled by AnimatedValue's
    /// lerpStateValue() dispatch, not by this free-standing helper.
    static QColor lerp(const QColor& from, const QColor& to, qreal t)
    {
        return detail::lerpColorLinear(from, to, t);
    }
    static qreal distance(const QColor& from, const QColor& to)
    {
        return detail::colorDistance(from, to);
    }
    static constexpr qreal retargetEpsilon = 1.0e-4;
    static bool isFinite(const QColor& c)
    {
        return std::isfinite(c.redF()) && std::isfinite(c.greenF()) && std::isfinite(c.blueF())
            && std::isfinite(c.alphaF());
    }
};

// QTransform polar-decomposed interpolation: decompose into translate +
// rotate + scale + shear, interpolate each independently, recompose.
// Component-wise lerp would shear during rotation.

namespace detail {

struct DecomposedTransform
{
    qreal tx = 0.0, ty = 0.0;
    qreal rotation = 0.0; // radians
    qreal sx = 1.0, sy = 1.0;
    qreal shear = 0.0;
};

/// QR-style decomposition on the 2x2 linear part under Qt's
/// post-multiply row-vector convention: M = R x K x S.
inline DecomposedTransform decomposeTransform(const QTransform& t)
{
    DecomposedTransform d;
    d.tx = t.dx();
    d.ty = t.dy();

    const qreal m11 = t.m11();
    const qreal m12 = t.m12();
    const qreal m21 = t.m21();
    const qreal m22 = t.m22();

    d.sx = std::sqrt(m11 * m11 + m21 * m21);
    d.rotation = d.sx > 1.0e-9 ? std::atan2(-m21, m11) : 0.0;

    const qreal cosR = std::cos(d.rotation);
    const qreal sinR = std::sin(d.rotation);

    d.sy = m22 * cosR + m12 * sinR;
    const qreal shearTimesSy = m12 * cosR - m22 * sinR;
    d.shear = qAbs(d.sy) > 1.0e-9 ? shearTimesSy / d.sy : 0.0;
    return d;
}

inline QTransform recomposeTransform(const DecomposedTransform& d)
{
    const qreal cosR = std::cos(d.rotation);
    const qreal sinR = std::sin(d.rotation);

    const qreal m11 = d.sx * cosR;
    const qreal m12 = d.sy * (d.shear * cosR + sinR);
    const qreal m21 = -d.sx * sinR;
    const qreal m22 = d.sy * (cosR - d.shear * sinR);

    return QTransform(m11, m12, m21, m22, d.tx, d.ty);
}

/// True if the 2x2 linear part is identity (only translation present).
inline bool isPureTranslate(const QTransform& t)
{
    constexpr qreal kIdentityEps = 1.0e-9;
    return qAbs(t.m11() - 1.0) < kIdentityEps && qAbs(t.m22() - 1.0) < kIdentityEps && qAbs(t.m12()) < kIdentityEps
        && qAbs(t.m21()) < kIdentityEps;
}

inline QTransform lerpTransform(const QTransform& from, const QTransform& to, qreal t)
{
    // Exact at endpoints — decompose/recompose is lossy (~1 ulp).
    if (t <= 0.0) {
        return from;
    }
    if (t >= 1.0) {
        return to;
    }

    // Reflection (det sign change) or near-singular endpoints: fall back
    // to component-wise lerp. Polar decomposition cannot smoothly
    // interpolate across a determinant sign change (must pass through
    // singular), and near-singular matrices decompose unreliably.
    constexpr qreal kNearSingularDet = 1.0e-6;
    const qreal detFrom = from.m11() * from.m22() - from.m12() * from.m21();
    const qreal detTo = to.m11() * to.m22() - to.m12() * to.m21();
    const bool reflection = detFrom * detTo < 0.0;
    const bool nearSingular = std::abs(detFrom) < kNearSingularDet || std::abs(detTo) < kNearSingularDet;
    if (reflection || nearSingular) {
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

    // Shortest-arc slerp: unwrap delta into (-pi, pi].
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
    /// Frobenius norm — mixes translation-pixels and rotation/scale units.
    /// Adequate for pure-translate retarget; mixed cases auto-degrade
    /// in AnimatedValue<QTransform>::retarget.
    static qreal distance(const QTransform& from, const QTransform& to)
    {
        const qreal d11 = to.m11() - from.m11();
        const qreal d12 = to.m12() - from.m12();
        const qreal d21 = to.m21() - from.m21();
        const qreal d22 = to.m22() - from.m22();
        const qreal dtx = to.dx() - from.dx();
        const qreal dty = to.dy() - from.dy();
        return std::sqrt(d11 * d11 + d12 * d12 + d21 * d21 + d22 * d22 + dtx * dtx + dty * dty);
    }
    static constexpr qreal retargetEpsilon = 0.5;
    static bool isFinite(const QTransform& t)
    {
        return std::isfinite(t.m11()) && std::isfinite(t.m12()) && std::isfinite(t.m21()) && std::isfinite(t.m22())
            && std::isfinite(t.dx()) && std::isfinite(t.dy());
    }
};

namespace detail {

/// Position-carrying T whose damage region is fully determined by endpoints + overshoot.
template<typename T>
concept PositionalGeometric = std::same_as<T, QPointF> || std::same_as<T, QRectF>;

/// Size-only T — caller must supply an anchor for damage rect.
template<typename T>
concept SizeGeometric = std::same_as<T, QSizeF>;

template<typename T>
concept ScalarValue = std::is_arithmetic_v<T>;

} // namespace detail

} // namespace PhosphorAnimation
