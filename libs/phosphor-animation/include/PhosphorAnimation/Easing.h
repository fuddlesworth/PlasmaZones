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

    Type type = Type::CubicBezier;

    /// Bezier control points (P1, P2). x clamped [0,1]; y clamped [-1,2].
    qreal x1 = 0.33;
    qreal y1 = 1.0;
    qreal x2 = 0.68;
    qreal y2 = 1.0;

    /// Elastic: oscillation intensity. Bounce: height scale. [0.5, 3.0].
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
