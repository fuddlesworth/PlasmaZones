// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>
#include <QtGlobal>

namespace PhosphorAnimation {

/// Parametric easing curve: cubic bezier, elastic, or bounce. All stateless.
/// Default construction yields OutCubic bezier (0.33, 1.00, 0.68, 1.00).
/// Value type with public fields — can be stack-allocated or stored inline.
class PHOSPHORANIMATION_EXPORT Easing final : public Curve
{
public:
    enum class Type {
        CubicBezier,
        ElasticIn,
        ElasticOut,
        ElasticInOut,
        BounceIn,
        BounceOut,
        BounceInOut,
    };

    Easing() = default;

    Easing(const Easing&) = default;
    Easing& operator=(const Easing&) = default;
    Easing(Easing&&) = default;
    Easing& operator=(Easing&&) = default;

    QString typeId() const override;
    qreal evaluate(qreal x) const override;
    QString toString() const override;
    std::unique_ptr<Curve> clone() const override;
    bool overshoots() const override;
    bool equals(const Curve& other) const override;

    /// Parse from config string. Returns default OutCubic on failure.
    static Easing fromString(const QString& str);

    /// Clamp @p amp to the range the given curve type actually honours. The two
    /// families share the `amplitude` field but not its meaning, so they do not
    /// share its bounds:
    ///
    /// - Elastic rings past the target, so its amplitude is what decides how far
    ///   out of [0, 1] the curve goes. It is bounded to [1.0, 2.0]. The ceiling
    ///   holds the curve's excursion to roughly [-1.6, 2.6], close enough to the
    ///   `Limits` overshoot envelope that the clip against it lasts a fraction of
    ///   a frame at an ordinary duration. (It does NOT bring elastic entirely
    ///   inside the envelope — that would need a cap near 1.3, which costs most of
    ///   the curve's range to buy back something too brief to see.) The floor is
    ///   not policy but arithmetic: the phase term `asin(1/a)` needs `a >= 1`, so
    ///   every value below 1.0 already behaved exactly like 1.0 — the old 0.5 floor
    ///   advertised half a range that did nothing.
    /// - Bounce never leaves [0, 1] at any amplitude (see `overshoots()`), so
    ///   the envelope has no claim on it and it keeps the wider [0.5, 3.0] range,
    ///   where the value scales dip depth and every part of it is live.
    static qreal clampAmplitude(Type t, qreal amp);

    Type type = Type::CubicBezier;

    /// Bezier control points (P1, P2). x clamped [0,1]; y clamped [-1,2].
    qreal x1 = 0.33;
    qreal y1 = 1.0;
    qreal x2 = 0.68;
    qreal y2 = 1.0;

    /// Elastic: oscillation intensity, [1.0, 2.0]. Bounce: height scale,
    /// [0.5, 3.0]. Clamp through `clampAmplitude()` — the ranges differ.
    qreal amplitude = 1.0;
    /// Elastic oscillation period. [0.1, 1.0].
    qreal period = 0.3;
    /// Bounce count. [1, 8].
    int bounces = 3;

    bool operator==(const Easing& other) const;
    bool operator!=(const Easing& other) const
    {
        return !(*this == other);
    }

private:
    static qreal evaluateElasticOut(qreal t, qreal amp, qreal per);
    static qreal evaluateBounceOut(qreal t, qreal amp, int n);
};

} // namespace PhosphorAnimation
