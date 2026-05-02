// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>
#include <QtGlobal>

#include <memory>

namespace PhosphorAnimation {

/// Mutable state for stateful curve progression (springs carry position+velocity
/// across frames; stateless curves share the struct for uniform caller treatment).
/// All time fields are in real seconds.
struct PHOSPHORANIMATION_EXPORT CurveState
{
    qreal value = 0.0;
    qreal velocity = 0.0;
    qreal time = 0.0;
    /// Start of current segment — stateless step() lerps from here to target.
    /// For retarget: set startValue = value, time = 0, then pass new target.
    /// Stateful curves (Spring) ignore this; they derive continuity from value/velocity.
    qreal startValue = 0.0;
    /// Segment duration in seconds. Stateful curves ignore this.
    qreal duration = 1.0;
};

/// Polymorphic base for all animation curves.
///
/// Uses a polymorphic hierarchy (not std::variant) so third parties can register
/// additional curve types at runtime via CurveRegistry. Immutable after
/// construction through the shared call path (shared_ptr<const Curve>).
///
/// Two progression models:
/// - Parametric (evaluate): value at normalized t in [0,1]. Fixed-duration.
/// - Stateful (step): advances CurveState by dt toward target. Physics integration.
///
/// Thread-safe: all const methods callable from any thread. step() mutates
/// only the caller-owned CurveState.
class PHOSPHORANIMATION_EXPORT Curve
{
public:
    virtual ~Curve() = default;

    /// Stable identifier for this curve subclass (e.g. "bezier", "spring").
    virtual QString typeId() const = 0;

    /// Evaluate at normalized time t in [0,1]. May overshoot by design.
    virtual qreal evaluate(qreal t) const = 0;

    /// Advance @p state by @p dt real seconds toward @p target.
    /// dt <= 0 is a no-op. Default impl maps to parametric evaluation
    /// using state.startValue/time/duration. For retarget with stateless
    /// curves: set startValue = value, time = 0 before changing target.
    virtual void step(qreal dt, CurveState& state, qreal target) const;

    /// True if this curve requires persistent CurveState across frames.
    virtual bool isStateful() const
    {
        return false;
    }

    /// True if this curve may evaluate outside [0,1] during progression.
    virtual bool overshoots() const
    {
        return false;
    }

    /// Approximate settle time in seconds. Parametric: 1.0 (the [0,1] domain).
    /// Spring: analytical 99% settle time. Never infinity.
    virtual qreal settleTime() const
    {
        return 1.0;
    }

    /// Serialize to "typeId:params" or bare "x1,y1,x2,y2" for cubic-bezier.
    /// 2-decimal float precision — round-trip is lossy.
    virtual QString toString() const = 0;

    /// Deep copy with identical parameters.
    virtual std::unique_ptr<Curve> clone() const = 0;

    /// Same typeId + same parameters. Default compares toString() (2-decimal
    /// rounded); subclasses with precise floats should override.
    virtual bool equals(const Curve& other) const;

protected:
    // Protected copy/move prevents slicing outside the hierarchy while
    // allowing subclasses to default their own copy/move for value semantics.
    Curve() = default;
    Curve(const Curve&) = default;
    Curve& operator=(const Curve&) = default;
    Curve(Curve&&) = default;
    Curve& operator=(Curve&&) = default;
};

} // namespace PhosphorAnimation
