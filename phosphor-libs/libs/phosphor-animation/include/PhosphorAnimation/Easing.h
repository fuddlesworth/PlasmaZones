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

    /// Highest peak an elastic curve may reach. It is `Limits::MaxCurveProgress`,
    /// and the equality is the point: because `amplitude` IS the peak, capping it
    /// here makes the overshoot envelope EXACT for elastic rather than a bound the
    /// curve gets clipped against. Elastic-out tops out at exactly 2.0 and
    /// elastic-in, its mirror, bottoms out at exactly -1.0, so the two of them span
    /// the envelope precisely and nothing is ever clipped.
    static constexpr qreal MaxElasticPeak = 2.0;

    static constexpr qreal MinElasticPeriod = 0.1;
    static constexpr qreal MaxElasticPeriod = 1.0;

    /// Bisection steps used to invert the peak into an internal gain. 24 steps over
    /// the [1, 16] bracket resolve the gain to ~1e-6, far below anything a pixel can
    /// show.
    static constexpr int ElasticSolveIterations = 24;

    /// The gentlest bounce elastic can make at @p period — the low end of the
    /// overshoot range, and it MOVES with the period.
    ///
    /// The curve is pinned to 0 at t = 0, a full unit below the target, so its wave
    /// cannot start with a magnitude below 1. At a short period the crest arrives
    /// before the envelope has decayed, so that unavoidable magnitude still shows up
    /// as a big first bounce: about 1.71 at period 0.1, versus 1.05 at period 1.0.
    /// Callers surface this as the slider's minimum instead of clamping into it
    /// silently, because a range that lies about what it can reach is how the old
    /// amplitude parameter went wrong in the first place.
    static qreal minElasticPeak(qreal period);

    /// Clamp @p amp to the range the given curve type actually honours. The two
    /// families share the `amplitude` field but not its meaning, so they do not
    /// share its bounds:
    ///
    /// - Elastic's amplitude is the PEAK the curve reaches, so its range is the
    ///   curve's own reachable one: `[minElasticPeak(period), MaxElasticPeak]`. It
    ///   depends on the period, which is why this takes one.
    /// - Bounce never leaves [0, 1] at any amplitude (see `overshoots()`), so the
    ///   envelope has no claim on it and it keeps the wider [0.5, 3.0] range, where
    ///   the value scales dip depth and every part of it is live. The period is
    ///   ignored.
    static qreal clampAmplitude(Type t, qreal amp, qreal period);

    Type type = Type::CubicBezier;

    /// Bezier control points (P1, P2). x clamped [0,1]; y clamped [-1,2].
    qreal x1 = 0.33;
    qreal y1 = 1.0;
    qreal x2 = 0.68;
    qreal y2 = 1.0;

    /// Elastic: the PEAK VALUE the curve reaches, so 1.5 means it travels 50% past
    /// its target before settling back. Range `[minElasticPeak(period), 2.0]`.
    ///
    /// This is deliberately not the classic Penner `amplitude`, which is the wave's
    /// magnitude at t = 0 — a quantity the curve never actually displays, because it
    /// is pinned to 0 there. Under that parameterisation the overshoot you SAW was
    /// the amplitude decayed by however long the crest took to arrive, so it moved
    /// with the period: one amplitude spanned a 5x range of real overshoot depending
    /// on where the period slider sat. The parameter now means the thing the user is
    /// actually choosing, and the internal Penner gain is solved for
    /// (`solveElasticGain`).
    ///
    /// Bounce: height scale, [0.5, 3.0] — unrelated meaning, see `clampAmplitude`.
    qreal amplitude = 1.0;
    /// Elastic oscillation period: how FAST it wiggles. It no longer sets how far
    /// the curve overshoots — `amplitude` does that on its own. [0.1, 1.0].
    qreal period = 0.3;
    /// Bounce count. [1, 8].
    int bounces = 3;

    bool operator==(const Easing& other) const;
    bool operator!=(const Easing& other) const
    {
        return !(*this == other);
    }

private:
    /// Invert `amplitude` (the peak) into the internal Penner gain that produces it
    /// at @p period. The relation is monotonic but transcendental, so it is solved
    /// by bisection; see the definition for why not Newton.
    static qreal solveElasticGain(qreal peak, qreal period);

    static qreal evaluateElasticOut(qreal t, qreal peak, qreal per);
    static qreal evaluateBounceOut(qreal t, qreal amp, int n);
};

} // namespace PhosphorAnimation
