// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>
#include <QtGlobal>

namespace PhosphorAnimation {

/// Damped harmonic oscillator. Params: omega (angular frequency, rad/s, [0.1,200])
/// and zeta (damping ratio, [0,10]). Matches Framer Motion / SwiftUI conventions.
/// evaluate() = analytical step-response; step() = semi-implicit Euler integration.
/// Wire format: "spring:omega,zeta".
class PHOSPHORANIMATION_EXPORT Spring final : public Curve
{
public:
    /// Hard ceiling on settleTime(), in seconds.
    ///
    /// A spring's analytical settle time diverges as `zeta*omega` approaches 0
    /// (an undamped spring never settles at all), so it is capped here to keep
    /// repaint budgets and timer delays bounded for pathological parameters.
    /// Public because callers reason about the bound: the compositor's shader
    /// lifetime, the geometry animator's completion test, and AnimationLimits.h
    /// all reference it, and test_spring asserts against it.
    static constexpr qreal MaxSettleSeconds = 30.0;

    Spring() = default;
    Spring(qreal omega, qreal zeta);

    Spring(const Spring&) = default;
    Spring& operator=(const Spring&) = default;
    Spring(Spring&&) = default;
    Spring& operator=(Spring&&) = default;

    QString typeId() const override;
    qreal evaluate(qreal t) const override;
    void step(qreal dt, CurveState& state, qreal target) const override;
    bool isStateful() const override
    {
        return true;
    }
    qreal settleTime() const override;
    QString toString() const override;
    std::unique_ptr<Curve> clone() const override;
    /// True for underdamped (zeta < 1). Critical/overdamped never overshoot.
    bool overshoots() const override;
    bool equals(const Curve& other) const override;

    /// Parse from "spring:omega,zeta" or "omega,zeta". Clamps to valid ranges.
    static Spring fromString(const QString& str);

    static Spring snappy(); ///< Responsive, slight overshoot.
    static Spring smooth(); ///< Critically damped, no overshoot.
    static Spring bouncy(); ///< Visible bounce.

    qreal omega = 12.0; ///< Angular natural frequency (rad/s). [0.1, 200].
    qreal zeta = 0.8; ///< Damping ratio. [0.0, 10.0].

    bool operator==(const Spring& other) const;
    bool operator!=(const Spring& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorAnimation
