// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>
#include <QtGlobal>

namespace PhosphorAnimation {

/**
 * @brief Parametric easing curve: cubic bezier, elastic, or bounce.
 *
 * Seven variants, all stateless (progression depends only on normalized
 * time t). Wire formats:
 *
 *   - CubicBezier:     `"x1,y1,x2,y2"`                (4 comma-separated floats)
 *   - ElasticIn:       `"elastic-in:amplitude,period"`
 *   - ElasticOut:      `"elastic-out:amplitude,period"`
 *   - ElasticInOut:    `"elastic-in-out:amplitude,period"`
 *   - BounceIn:        `"bounce-in:amplitude,bounces"`
 *   - BounceOut:       `"bounce-out:amplitude,bounces"`
 *   - BounceInOut:     `"bounce-in-out:amplitude,bounces"`
 *
 * Each curve has exactly one wire format — there is no parallel
 * `"bezier:x,y,x,y"` form, intentionally. The bare 4-comma form is
 * unambiguous (no letters except float exponents) and that's what
 * `Easing::toString` emits.
 *
 * Default construction yields OutCubic bezier (0.33, 1.00, 0.68, 1.00).
 *
 * ## Value semantics
 *
 * `Easing` is a value type with public fields. Copy/move are explicit
 * to keep `Curve`'s protected base from blocking them, but otherwise
 * Easing behaves like any POD-ish struct: it can be stack-allocated,
 * stored inline in `WindowMotion`, copied freely, etc. The polymorphic
 * `Curve` dispatch path is only engaged when an `Easing` is wrapped in
 * a `shared_ptr<const Curve>`.
 */
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

    // Value-type copy/move: Easing is held by value in WindowMotion and
    // anywhere else that wants a stack-allocated stateless curve. The
    // protected base copy in Curve.h makes this safe from slicing
    // outside the hierarchy.
    Easing(const Easing&) = default;
    Easing& operator=(const Easing&) = default;
    Easing(Easing&&) = default;
    Easing& operator=(Easing&&) = default;

    // ─────── Curve overrides ───────

    QString typeId() const override;
    qreal evaluate(qreal x) const override;
    QString toString() const override;
    std::unique_ptr<Curve> clone() const override;
    bool overshoots() const override;

    /// Overrides `Curve::equals()` to delegate to the tight
    /// `operator==`. The base default compares `toString()` forms,
    /// which are 2-decimal-rounded and would call curves equal that
    /// differ by up to 0.005 in any parameter.
    bool equals(const Curve& other) const override;

    // ─────── Easing-specific API ───────

    /**
     * @brief Parse from a config string into a value-type @c Easing.
     *
     * Accepts the bare cubic-bezier wire form ("0.33,1.00,0.68,1.00")
     * and the named-curve form ("elastic-out:1.0,0.3", "bounce-in:1.5,4").
     * Returns default OutCubic bezier on any parse failure.
     *
     * Use this when the caller wants a value-type @c Easing (e.g., to
     * stack-allocate or store inline). Use @ref CurveRegistry::create
     * when the caller wants a polymorphic `shared_ptr<const Curve>` and
     * may need Spring or third-party curve types from the same string
     * format. The two share the same parsing rules.
     */
    static Easing fromString(const QString& str);

    // ─────── Parameters (public — Easing is a value-type aggregate) ───────

    Type type = Type::CubicBezier;

    /// Bezier control points (CubicBezier only). P0=(0,0), P3=(1,1)
    /// are implicit; these are P1=(x1,y1) and P2=(x2,y2). x is clamped
    /// to [0,1] on parse; y is clamped to [-1, 2] to permit overshoot.
    qreal x1 = 0.33;
    qreal y1 = 1.0;
    qreal x2 = 0.68;
    qreal y2 = 1.0;

    /// Elastic: oscillation overshoot intensity (≥ 1 amplifies).
    /// Bounce: bounce height scale.
    /// Clamped to [0.5, 3.0] on parse.
    qreal amplitude = 1.0;

    /// Elastic oscillation period. Clamped to [0.1, 1.0] on parse.
    qreal period = 0.3;

    /// Bounce count. Clamped to [1, 8] on parse.
    int bounces = 3;

    // ─────── Value-type equality ───────

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
