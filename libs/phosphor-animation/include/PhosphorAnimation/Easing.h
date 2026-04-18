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
 * time t). Serialized strings:
 *
 *   - CubicBezier:     `"x1,y1,x2,y2"`                (legacy bare form)
 *                      `"bezier:x1,y1,x2,y2"`         (registry form)
 *   - ElasticIn:       `"elastic-in:amplitude,period"`
 *   - ElasticOut:      `"elastic-out:amplitude,period"`
 *   - ElasticInOut:    `"elastic-in-out:amplitude,period"`
 *   - BounceIn:        `"bounce-in:amplitude,bounces"`
 *   - BounceOut:       `"bounce-out:amplitude,bounces"`
 *   - BounceInOut:     `"bounce-in-out:amplitude,bounces"`
 *
 * Default construction yields OutCubic bezier (0.33, 1.00, 0.68, 1.00).
 *
 * ## Value semantics
 *
 * Easing preserves value-type semantics for legacy back-compat (callers
 * that hold `Easing` by value, copy it, or use it as a struct field
 * continue to work after the compositor-common shim). The underlying
 * polymorphic Curve dispatch only engages when accessed through
 * `shared_ptr<const Curve>` in the new API.
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

    // Value-type copy/move: required for legacy callers that hold Easing
    // by value (e.g. AnimationConfig::easing). Protected base copy in
    // Curve.h makes this safe from slicing outside the hierarchy.
    Easing(const Easing&) = default;
    Easing& operator=(const Easing&) = default;
    Easing(Easing&&) = default;
    Easing& operator=(Easing&&) = default;

    // ─────── Curve overrides ───────

    QString typeId() const override;
    qreal evaluate(qreal x) const override;
    QString toString() const override;
    std::unique_ptr<Curve> clone() const override;
    // equals() uses Curve::equals() default (typeId + toString round-trip).
    // Not overridden — string form is lossless within our 2-decimal format.

    // ─────── Easing-specific API ───────

    /**
     * @brief Parse from a config string.
     *
     * Accepts both the legacy bare bezier form ("0.33,1.00,0.68,1.00")
     * and prefixed forms ("elastic-out:1.0,0.3", "bezier:0.33,1,0.68,1").
     * Returns default OutCubic bezier on any parse failure.
     *
     * @note This is a static factory. Prefer CurveRegistry::create() in
     * new code since it also handles Spring and future curve types.
     */
    static Easing fromString(const QString& str);

    // ─────── Parameters (public for legacy back-compat) ───────

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

    // ─────── Legacy equality (value-type compat) ───────

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
