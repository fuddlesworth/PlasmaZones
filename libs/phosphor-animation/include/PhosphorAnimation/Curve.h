// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QString>
#include <QtGlobal>

#include <memory>

namespace PhosphorAnimation {

/**
 * @brief Mutable state for stateful curve progression.
 *
 * Springs and other physics-based curves need to carry position+velocity
 * across frames; stateless parametric curves (easing) don't use velocity
 * but share the same struct so callers can treat all curves uniformly.
 *
 * All time fields are in real seconds so `step()` has a single consistent
 * `dt` contract across stateless and stateful curves. AnimatedValue<T>
 * (Phase 3) will map to/from concrete typed ranges.
 */
struct PHOSPHORANIMATION_EXPORT CurveState
{
    /// Current value. Usually [0, 1]; elastic/bounce/spring may overshoot.
    qreal value = 0.0;
    /// Rate of change per second. Springs read+write this; stateless
    /// curves numerically derive it from the parametric progression.
    qreal velocity = 0.0;
    /// Elapsed time since the current segment began, in real seconds.
    qreal time = 0.0;
    /// Value at the start of the current animation segment. Used by the
    /// default `Curve::step()` to produce `lerp(startValue, target,
    /// evaluate(t))` so stateless curves respect whatever value the
    /// caller held when the animation began — and so retarget-by-reset
    /// (set `startValue = value; time = 0;` then pass the new target)
    /// produces continuous motion. Stateful subclasses like Spring
    /// ignore this and derive continuity from `value`/`velocity` alone.
    qreal startValue = 0.0;
    /// Segment duration in real seconds. Used by the default stateless
    /// `step()` to map `state.time → t = time/duration`. Stateful curves
    /// (Spring) ignore this; they derive settle time from their own
    /// parameters. Zero or negative means "complete immediately" for
    /// stateless curves.
    qreal duration = 1.0;
};

/**
 * @brief Polymorphic base for all animation curves.
 *
 * PhosphorAnimation uses a **polymorphic** curve hierarchy (not a closed
 * `std::variant`) so third parties — plugins, shell extensions, user
 * scripts — can register additional curve types at runtime via
 * CurveRegistry. This is the design required for niri / Hyprland /
 * Quickshell-level customization where users define their own curves in
 * config and the shell looks them up by name.
 *
 * ## Immutability through the polymorphic path
 *
 * The shared call path is `std::shared_ptr<const Curve>` — holding one
 * forbids mutation through the base pointer. Concrete subclasses like
 * `Easing` and `Spring` also support value-type semantics so legacy
 * callers can keep them inline in structs (e.g., `AnimationConfig`); a
 * value-held instance is mutable by its owner, but it must not be
 * mutated after being wrapped in a `shared_ptr`. In practice the only
 * safe mutation pattern is "build then freeze": construct, configure,
 * hand the finished object to a shared_ptr, done.
 *
 * ## Two progression models
 *
 * - **Parametric** (evaluate): `evaluate(t)` returns the curve's value
 *   at normalized time `t ∈ [0, 1]`. Suitable for fixed-duration
 *   animations where the caller owns the clock. Easing curves are
 *   pure parametric.
 *
 * - **Stateful** (step): `step(dt, state, target)` advances `state` by
 *   `dt` seconds toward `target`. Used for true physics integration
 *   (spring with real velocity continuity under retarget). The default
 *   base implementation maps to parametric evaluation with `startValue`
 *   held in `CurveState`, so stateless curves can also drive `step()`
 *   based animations if callers reset `startValue` + `time` on
 *   retarget.
 *
 * ## Thread safety
 *
 * Curve subclasses must be immutable after construction (see above).
 * `evaluate()`, `toString()`, `typeId()`, `clone()`, `equals()`,
 * `isStateful()`, and `settleTime()` are all safe to call from any
 * thread. `step()` mutates the caller-owned `CurveState&` — the curve
 * itself is untouched.
 */
class PHOSPHORANIMATION_EXPORT Curve
{
public:
    virtual ~Curve() = default;

    /**
     * @brief Stable identifier for this curve subclass.
     *
     * Used by CurveRegistry for string-form lookup and plugin extension.
     * Examples: "bezier", "elastic-out", "bounce-in-out", "spring".
     * Distinct Curve subclasses MUST use distinct typeIds.
     */
    virtual QString typeId() const = 0;

    /**
     * @brief Evaluate the curve at normalized time @p t ∈ [0, 1].
     *
     * Return value is usually in [0, 1] but elastic / bounce / spring
     * curves may overshoot by design (e.g., 1.05 or -0.02). Callers
     * should not clamp the return.
     *
     * For stateful curves (Spring), this provides the analytical
     * underdamped-oscillator progression — useful for fixed-duration
     * animations where true physics integration isn't required.
     */
    virtual qreal evaluate(qreal t) const = 0;

    /**
     * @brief Advance @p state by @p dt real seconds toward @p target.
     *
     * `dt` is always real seconds — the same contract across every curve
     * subclass so callers holding `std::shared_ptr<const Curve>` can drive
     * step() polymorphically without knowing the concrete type.
     *
     * Stateful curves (Spring) read `state.value` + `state.velocity` and
     * write the integrated next step using their own physics. Retarget
     * mid-flight is natural — callers just change `target` between calls
     * and velocity is preserved.
     *
     * The default implementation is for stateless curves: it increments
     * `state.time` by `dt`, computes `t = state.time / state.duration`
     * clamped to [0, 1], then sets
     * `state.value = lerp(state.startValue, target, evaluate(t))`.
     * `state.velocity` is overwritten with the numerical derivative of
     * the parametric progression for this step — it is NOT a preserved
     * physical velocity. Switching from a stateful curve to a stateless
     * one therefore resets velocity to the parametric-derivative value.
     *
     * For **retarget** with a stateless curve: set `state.startValue =
     * state.value` and `state.time = 0` before changing `target`. That
     * mid-flight reset produces continuous motion because the new
     * segment starts at the current value. Stateful curves handle
     * retarget implicitly and ignore `startValue`/`duration`.
     */
    virtual void step(qreal dt, CurveState& state, qreal target) const;

    /**
     * @brief True if this curve requires persistent `CurveState` to
     * produce correct results across frames.
     *
     * Spring: true (velocity + position carry forward).
     * Easing: false (parametric evaluation at t is sufficient).
     *
     * Callers use this as a hint when choosing between `evaluate()` and
     * `step()` in AnimatedValue / WindowMotion.
     */
    virtual bool isStateful() const
    {
        return false;
    }

    /**
     * @brief Approximate time (seconds) for a stateful curve to settle to
     * its target within epsilon.
     *
     * Used to bound paint regions and animation duration budgets:
     *
     * - Parametric curves (Easing): returns 1.0 — the domain is [0, 1].
     *   Callers scale by their own `duration` externally.
     * - Spring: returns the analytical 99% settle time derived from the
     *   damping ratio and angular frequency.
     *
     * Never returns infinity. Overdamped springs with zeta → ∞ still
     * return a finite value clamped to a library-defined maximum.
     */
    virtual qreal settleTime() const
    {
        return 1.0;
    }

    /**
     * @brief Serialize curve parameters + typeId to a stable string form.
     *
     * Format: `"typeId:params"` for named curves (e.g.,
     * `"elastic-out:1.0,0.3"`, `"spring:12.0,0.8"`), or the bare
     * `"x1,y1,x2,y2"` legacy format for cubic-bezier back-compat.
     *
     * The string form uses 2-decimal float precision for human
     * readability — that makes the round-trip **lossy**. `Easing` and
     * `Spring` therefore override `equals()` to forward to their own
     * tight float comparisons; do the same in any subclass whose
     * `toString()` is similarly rounded, or else two visually-distinct
     * curves with parameters differing below 0.005 will compare equal.
     */
    virtual QString toString() const = 0;

    /// Deep copy. Returns an owned mutable snapshot with identical params.
    /// Callers who want to keep the clone immutable should wrap it in a
    /// `std::shared_ptr<const Curve>` themselves.
    virtual std::unique_ptr<Curve> clone() const = 0;

    /**
     * @brief Equality: same typeId + same subclass-relevant parameters.
     *
     * Default implementation compares `typeId()` and `toString()` —
     * a safe fallback, but the string form is 2-decimal-rounded so the
     * default is **not** tight enough to distinguish curves differing
     * only below that precision. Subclasses with precise float
     * parameters SHOULD override this to forward to their value-type
     * `operator==`, as `Easing` and `Spring` do.
     */
    virtual bool equals(const Curve& other) const;

protected:
    // Protected copy/move prevents slicing via `Curve base = derived;`
    // outside the hierarchy, while still allowing subclasses (Easing,
    // Spring) to `= default` their own copy/move for value semantics.
    // This is the Scott Meyers idiom for polymorphic value types.
    Curve() = default;
    Curve(const Curve&) = default;
    Curve& operator=(const Curve&) = default;
    Curve(Curve&&) = default;
    Curve& operator=(Curve&&) = default;
};

} // namespace PhosphorAnimation
