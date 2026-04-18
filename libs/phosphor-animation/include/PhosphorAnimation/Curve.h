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
 * @note Values are dimensionless normalized units in [0, 1] unless a
 * specific caller interprets them otherwise. AnimatedValue<T> (Phase 3)
 * will map to/from concrete typed ranges.
 */
struct PHOSPHORANIMATION_EXPORT CurveState
{
    /// Current value. Usually [0, 1]; elastic/bounce/spring may overshoot.
    qreal value = 0.0;
    /// Rate of change per second. Springs read+write this; easing ignores.
    qreal velocity = 0.0;
    /// Elapsed time since start, in seconds. Parametric curves advance
    /// this via step() and evaluate(time/duration) under the hood.
    qreal time = 0.0;
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
 * Curves are **immutable** after construction. Pass by
 * `std::shared_ptr<const Curve>` — cheap copy, safe to share across
 * threads, and multiple AnimatedValues / WindowMotions / Profiles can
 * reference the same curve definition.
 *
 * ## Two progression models
 *
 * - **Parametric** (evaluate): `evaluate(t)` returns the curve's value at
 *   normalized time `t ∈ [0, 1]`. Suitable for fixed-duration animations
 *   where the caller owns the clock. Easing curves are pure parametric.
 *
 * - **Stateful** (step): `step(dt, state, target)` advances `state` by
 *   `dt` seconds toward `target`. Used for true physics integration
 *   (spring with real velocity continuity under retarget). Stateless
 *   curves provide a default `step()` that falls back to evaluate().
 *
 * ## Thread safety
 *
 * Curve subclasses must be immutable after construction. `evaluate()`,
 * `toString()`, `typeId()`, `clone()`, `equals()`, `isStateful()`, and
 * `settleTime()` are all safe to call from any thread. `step()` mutates
 * the caller-owned `CurveState&` — the curve itself is untouched.
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
     * @brief Advance @p state by @p dt seconds toward @p target.
     *
     * Stateful curves (Spring) read `state.value` + `state.velocity` and
     * write the integrated next step using their own physics. Retarget
     * mid-flight is natural — callers just change `target` between calls
     * and velocity is preserved.
     *
     * The default implementation is for stateless curves: it increments
     * `state.time` by `dt`, recomputes `state.value = evaluate(t)` using
     * a duration of 1.0 (so callers must scale `dt` by 1/duration before
     * calling), and computes `state.velocity` as the approximate
     * numerical derivative.
     *
     * @note Subclasses that override `step()` should consult their own
     * `settleTime()` to decide when `state.value` has converged.
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
     * Round-trips through CurveRegistry::create() → curve → toString().
     */
    virtual QString toString() const = 0;

    /// Deep copy. Returns an owned mutable snapshot with identical params.
    virtual std::unique_ptr<Curve> clone() const = 0;

    /**
     * @brief Equality: same typeId + same subclass-relevant parameters.
     *
     * Default implementation compares `typeId()` and `toString()` — safe
     * fallback for any subclass whose toString() round-trip is lossless.
     * Subclasses with non-lossless string forms (e.g., floating-point
     * parameters that serialize with reduced precision) should override.
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
