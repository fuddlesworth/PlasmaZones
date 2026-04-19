// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/RetargetPolicy.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QLineF>
#include <QLoggingCategory>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <concepts>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace PhosphorAnimation {

// Cross-library export: AnimatedValue<T> is a header-only template, so
// the category is referenced from consumer translation units but defined
// in phosphor-animation's animatedvalue.cpp. Must be exported so the
// symbol resolves across the SO boundary.
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(lcAnimatedValue, PHOSPHORANIMATION_EXPORT)

// ═══════════════════════════════════════════════════════════════════════════════
// Interpolate<T> — per-type lerp + distance helpers
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Type-specific linear interpolation and path-distance for
 *        `AnimatedValue<T>`.
 *
 * `AnimatedValue<T>` progresses a *scalar* normalized parameter in [0, 1]
 * (the curve's `evaluate()` / `step()` output) and uses this helper to
 * lerp concrete T-values along the start→target segment. Specialisations
 * shipped in this header cover `qreal`, `QPointF`, `QSizeF`, `QRectF`.
 * Phase 3 sub-commit 4 adds `QColor` (linear-space + OkLab opt-in) and
 * `QTransform` (polar-decomposed lerp).
 *
 * `distance` is used by the `PreserveVelocity` retarget path — world-
 * space rate is scalar-by-magnitude, so vector specialisations compute
 * Euclidean magnitude. Direction is not preserved across retarget for
 * vector T (see Phase 3 decision B / design doc note on retarget
 * semantics).
 */
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
};

template<>
struct Interpolate<QRectF>
{
    static QRectF lerp(const QRectF& from, const QRectF& to, qreal t)
    {
        return QRectF(Interpolate<QPointF>::lerp(from.topLeft(), to.topLeft(), t),
                      Interpolate<QSizeF>::lerp(from.size(), to.size(), t));
    }
    static qreal distance(const QRectF& from, const QRectF& to)
    {
        // 4-D Euclidean norm over (x, y, w, h) — matches the behaviour
        // the snap-animation path in Phase 2 implicitly used when it
        // combined position and size into a single animation progress.
        const qreal dp = Interpolate<QPointF>::distance(from.topLeft(), to.topLeft());
        const qreal ds = Interpolate<QSizeF>::distance(from.size(), to.size());
        return std::sqrt(dp * dp + ds * ds);
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Concept guards for the bounds() / sweptRange() members
// ═══════════════════════════════════════════════════════════════════════════════

namespace detail {

template<typename T>
concept GeometricValue = std::same_as<T, QPointF> || std::same_as<T, QSizeF> || std::same_as<T, QRectF>;

template<typename T>
concept ScalarValue = std::is_arithmetic_v<T>;

} // namespace detail

// ═══════════════════════════════════════════════════════════════════════════════
// AnimatedValue<T>
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief The unified motion primitive. One value of type T transitioning
 *        from a start to a target over time, driven by a curve and a
 *        clock.
 *
 * Phase 3 replaces the Phase-2 `WindowMotion` struct with this template.
 * `AnimatedValue<QRectF>` is the snap-animation primitive; other
 * specialisations drive shell UI (opacity, colour, transform, scalar
 * shader uniforms). See the design doc's Phase 3 section for the full
 * context — decision B (unification), decision D (clock injection),
 * decision H (MotionSpec-vs-Profile split), decision J (callback-driven
 * damage).
 *
 * ## Ownership and lifetime
 *
 * Move-only. A live animation cannot be duplicated because its
 * `onValueChanged` / `onComplete` callbacks are single-owner contracts —
 * copying would invoke both sides and break assumptions at the consumer
 * (addRepaint called twice, polishAndUpdate fighting itself). Move is
 * fine: the target instance takes over the callbacks and the state, the
 * source is left empty.
 *
 * Clocks are held by non-owning pointer (stored inside `MotionSpec`).
 * Every AnimatedValue must outlive its clock — typically the clock is
 * a process-long singleton per output / per `QQuickWindow`, so this is
 * trivially satisfied.
 *
 * ## Progression model
 *
 * - **Stateful curves (`Spring`):** `advance()` calls `curve->step(dt,
 *   state, 1.0)` with real-seconds `dt` derived from
 *   `clock.now() - lastTick`. `state.value` tracks normalized progress
 *   [0, 1]; `state.velocity` is dProgress/dt. Completion when
 *   `state.value` snaps to 1.0 AND `|state.velocity|` is zero
 *   (Spring::step's internal convergence lock), or when elapsed
 *   exceeds a 60-second safety cap.
 * - **Stateless curves (`Easing`):** `advance()` computes
 *   `t = elapsed_ms / profile.effectiveDuration()`, calls
 *   `state.value = curve->evaluate(t)`. Completion when `elapsed_ms >=
 *   duration`; `state.value` is clamped to exactly 1.0 on the terminal
 *   frame so Spring-shaped parametric curves (e.g., under-damped
 *   approximation) do not paint a final-frame miss.
 *
 * In both cases, `value()` returns
 * `Interpolate<T>::lerp(from, to, state.value)`.
 *
 * ## Retarget semantics
 *
 * `retarget(newTo, policy)` preserves the visible value (new segment
 * starts from current value) and reshapes state per the policy (see
 * `RetargetPolicy` docs). `PreserveVelocity` on a stateful curve
 * re-projects the scalar `state.velocity` onto the new segment's
 * normalized space so the *world-space rate of change* is continuous
 * across the retarget boundary. On a stateless curve, `PreserveVelocity`
 * degrades to `PreservePosition` with a debug log (there is no physical
 * velocity on a parametric curve).
 *
 * ## Thread safety
 *
 * GUI-thread only — matches `IMotionClock`'s contract.
 */
template<typename T>
class AnimatedValue
{
public:
    AnimatedValue() = default;
    ~AnimatedValue() = default;

    AnimatedValue(const AnimatedValue&) = delete;
    AnimatedValue& operator=(const AnimatedValue&) = delete;
    AnimatedValue(AnimatedValue&&) noexcept = default;
    AnimatedValue& operator=(AnimatedValue&&) noexcept = default;

    // ─────── Lifecycle ───────

    /**
     * @brief Begin an animation from @p from to @p to driven by @p spec.
     *
     * Validates required fields:
     *   - `spec.clock` must be non-null.
     *   - An effective curve must be derivable (either
     *     `spec.profile.curve` is non-null, or a default Easing is used
     *     — this matches the Phase-2 CurveRegistry fallback).
     *
     * The animation's `startTime` is latched on the first `advance()`
     * call, not here — this matches Phase 2's `WindowMotion` semantics
     * and avoids the "what if start() is called between paint cycles?"
     * problem (the next paint's clock reading becomes t=0, no matter
     * when start() fired).
     *
     * @return `true` on success. `false` if a required field is missing
     *         or `from == to` (degenerate — no motion); in the degenerate
     *         case the value is snapped to target and `isComplete()`
     *         becomes true immediately without firing callbacks.
     */
    bool start(T from, T to, MotionSpec<T> spec)
    {
        if (!spec.clock) {
            qCWarning(lcAnimatedValue) << "start() rejected: null clock";
            return false;
        }

        m_from = std::move(from);
        m_to = std::move(to);
        m_spec = std::move(spec);
        m_current = m_from;
        m_state = CurveState{};
        m_state.startValue = 0.0;
        m_state.duration = 1.0; // normalised; real time comes from clock dt
        m_startTime.reset();
        m_lastTickTime.reset();

        // Degenerate: start == target with no motion. Snap to target,
        // mark complete, fire no callbacks (start() is the contract
        // boundary; the "nothing happened" path is silent to match how
        // Phase 2's createSnapMotion returned nullopt for zero motion).
        if (qFuzzyIsNull(Interpolate<T>::distance(m_from, m_to))) {
            m_current = m_to;
            m_state.value = 1.0;
            m_isAnimating = false;
            m_isComplete = true;
            return false;
        }

        m_isAnimating = true;
        m_isComplete = false;
        m_spec.clock->requestFrame();
        return true;
    }

    /**
     * @brief Redirect the in-flight animation to a new target.
     *
     * The new segment runs from the *current* visible value to @p newTo,
     * so there is no visual jump at the boundary. Velocity treatment
     * follows @p policy (see `RetargetPolicy`).
     *
     * A `retarget()` call on an AnimatedValue that is not currently
     * animating is equivalent to `start(currentValue, newTo,
     * storedSpec)` — the stored spec is reused. If no spec has been
     * stored (never started), this returns false.
     */
    bool retarget(T newTo, RetargetPolicy policy)
    {
        if (!m_spec.clock) {
            qCWarning(lcAnimatedValue) << "retarget() rejected: no stored spec (never started)";
            return false;
        }

        const T newFrom = m_current;

        // Compute the re-projected normalised velocity *before* we
        // overwrite m_from / m_to — we need the old segment's distance.
        const qreal oldDistance = Interpolate<T>::distance(m_from, m_to);
        const qreal newDistance = Interpolate<T>::distance(newFrom, newTo);

        const auto curve = effectiveCurve();
        const bool stateful = curve && curve->isStateful();

        qreal newVelocity = 0.0;
        switch (policy) {
        case RetargetPolicy::PreserveVelocity:
            if (stateful && newDistance > 0.0) {
                // Map scalar normalised-velocity through the world:
                //   worldRate [T-units/s] = state.velocity [1/s] * oldDistance [T-units]
                //   newNormalisedVelocity [1/s] = worldRate / newDistance
                // For vector T, distance() returns magnitude — direction
                // is lost (documented limitation; see header intro).
                newVelocity = (m_state.velocity * oldDistance) / newDistance;
            } else if (!stateful) {
                qCDebug(lcAnimatedValue) << "PreserveVelocity degrading to PreservePosition on stateless curve"
                                         << (curve ? curve->typeId() : QStringLiteral("null"));
            }
            break;
        case RetargetPolicy::ResetVelocity:
            newVelocity = 0.0;
            break;
        case RetargetPolicy::PreservePosition:
            // Stateful: velocity zeroed; motion restarts from rest
            //   toward the new target. Stateless: identical (no
            //   velocity concept). This is the explicit "position
            //   continuity only" policy.
            newVelocity = 0.0;
            break;
        }

        m_from = newFrom;
        m_to = std::move(newTo);
        m_current = m_from;
        m_state.value = 0.0;
        m_state.velocity = newVelocity;
        m_state.time = 0.0;
        m_state.startValue = 0.0;
        m_startTime.reset();
        m_lastTickTime.reset();

        if (qFuzzyIsNull(newDistance)) {
            // Re-targeting to the same point we're already at —
            // complete-in-place, no motion.
            m_current = m_to;
            m_state.value = 1.0;
            m_state.velocity = 0.0;
            m_isAnimating = false;
            m_isComplete = true;
            return false;
        }

        m_isAnimating = true;
        m_isComplete = false;
        m_spec.clock->requestFrame();
        return true;
    }

    /// Convenience overload using `m_spec.retargetPolicy`.
    bool retarget(T newTo)
    {
        return retarget(std::move(newTo), m_spec.retargetPolicy);
    }

    /**
     * @brief Stop animating; leave `value()` at its current position.
     *
     * Does not fire `onComplete` — cancellation is explicitly non-
     * completion. Consumers that need cleanup on both paths observe
     * cancellation via their own logic (e.g., after calling cancel()
     * the caller knows the animation was interrupted; onComplete
     * exists for natural termination only).
     */
    void cancel()
    {
        m_isAnimating = false;
        m_isComplete = false;
    }

    /**
     * @brief Force the animation to its target value immediately.
     *
     * Snaps `value()` to target, marks complete, and fires
     * `onComplete` exactly once. Used by consumers that want to
     * collapse an in-flight animation (e.g., window minimize that
     * interrupts an in-flight snap — the final geometry is what
     * matters, not the interpolation).
     */
    void finish()
    {
        if (!m_isAnimating && !m_isComplete) {
            return; // Never started — nothing to finish.
        }
        m_current = m_to;
        m_state.value = 1.0;
        m_state.velocity = 0.0;
        m_isAnimating = false;
        m_isComplete = true;
        if (m_spec.onValueChanged) {
            m_spec.onValueChanged(m_current);
        }
        if (m_spec.onComplete) {
            m_spec.onComplete();
        }
    }

    // ─────── Per-frame ───────

    /**
     * @brief Advance the animation by one paint tick.
     *
     * Reads `clock.now()`, derives `dt` since the last advance, steps
     * the curve, updates `value()`, and fires `onValueChanged`
     * (always, on every tick including the first one that latches
     * startTime) + `onComplete` (exactly once when the curve settles).
     *
     * The first `advance()` after `start()` or `retarget()` latches
     * startTime and lastTickTime and produces `value() == from` with
     * zero progress — the curve has not stepped yet. Subsequent ticks
     * step the curve with `dt = now - lastTickTime`.
     *
     * No-op when not animating. Safe to call every paint cycle
     * regardless of animation state.
     */
    void advance()
    {
        if (!m_isAnimating || !m_spec.clock) {
            return;
        }

        const auto now = m_spec.clock->now();

        if (!m_startTime) {
            // First tick: latch timestamps, emit the start value, and
            // request another frame so we actually progress. The curve
            // has not stepped; state.value stays 0 this tick.
            m_startTime = now;
            m_lastTickTime = now;
            m_current = m_from;
            if (m_spec.onValueChanged) {
                m_spec.onValueChanged(m_current);
            }
            m_spec.clock->requestFrame();
            return;
        }

        const auto elapsed = now - *m_startTime;
        const qreal dtSeconds = std::chrono::duration<qreal>(now - *m_lastTickTime).count();
        m_lastTickTime = now;

        // Defensive: negative dt indicates a non-monotonic clock
        // (IMotionClock mandates monotonicity; this is belt-and-
        // suspenders). Treat as zero-step and request another frame.
        if (dtSeconds < 0.0) {
            m_spec.clock->requestFrame();
            return;
        }

        const auto curve = effectiveCurve();
        const qreal durationMs = m_spec.profile.effectiveDuration();
        const qreal elapsedMs = std::chrono::duration<qreal, std::milli>(elapsed).count();

        bool complete = false;

        if (curve->isStateful()) {
            // Physics-driven progression: curve integrates at real dt
            // toward target=1.0. Spring snaps state.value=1.0 &&
            // velocity=0 on convergence.
            curve->step(dtSeconds, m_state, 1.0);

            if (m_state.value >= 1.0 && qAbs(m_state.velocity) <= 1.0e-6) {
                complete = true;
            } else if (elapsed > kSafetyCap) {
                // Pathological runaway guard. If a stateful curve
                // fails to converge within 60 s, force-complete. Spring
                // caps its own settleTime at 30 s, so this is only
                // triggered by a misconfigured curve or a clock that
                // stopped advancing mid-animation.
                qCWarning(lcAnimatedValue) << "stateful curve exceeded safety cap; forcing completion";
                complete = true;
            }
        } else {
            // Parametric progression: t = elapsed / duration.
            if (durationMs <= 0.0 || elapsedMs >= durationMs) {
                // Terminal frame: snap to exactly 1.0 regardless of
                // curve shape (matches Phase 2's updateProgress
                // terminal-frame clamp; Spring-like parametric curves
                // whose evaluate(1) sits in the settle band would
                // otherwise paint a visible miss on the final frame).
                m_state.value = 1.0;
                complete = true;
            } else {
                const qreal t = elapsedMs / durationMs;
                m_state.value = curve->evaluate(t);
            }
        }

        if (complete) {
            m_current = m_to;
            m_state.value = 1.0;
            m_state.velocity = 0.0;
            m_isAnimating = false;
            m_isComplete = true;
            if (m_spec.onValueChanged) {
                m_spec.onValueChanged(m_current);
            }
            if (m_spec.onComplete) {
                m_spec.onComplete();
            }
            // No clock->requestFrame() here — completion means no
            // further ticks are needed. The consumer's completion
            // callback can still schedule its own final paint.
        } else {
            m_current = Interpolate<T>::lerp(m_from, m_to, m_state.value);
            if (m_spec.onValueChanged) {
                m_spec.onValueChanged(m_current);
            }
            m_spec.clock->requestFrame();
        }
    }

    // ─────── Queries ───────

    /// Current interpolated value. Cheap (returns a cached field).
    T value() const
    {
        return m_current;
    }

    /// Normalised rate of change (1/s). Meaningful for stateful curves
    /// (Spring); for stateless curves this is the numerical derivative
    /// written by the default `Curve::step()` — usable as a hint but
    /// not a physical velocity.
    qreal velocity() const
    {
        return m_state.velocity;
    }

    /// Curve state snapshot. Exposed for consumers (AnimationController's
    /// retarget helpers, tests) that need to read raw progression state
    /// without going through `value()`.
    const CurveState& state() const
    {
        return m_state;
    }

    bool isAnimating() const
    {
        return m_isAnimating;
    }
    bool isComplete() const
    {
        return m_isComplete;
    }

    /// Access to the stored spec — consumers rarely need this directly,
    /// but tests and AnimationController::retarget() read the clock /
    /// profile back out for inspection.
    const MotionSpec<T>& spec() const
    {
        return m_spec;
    }

    /// Start point of the *current* segment (post-retarget).
    T from() const
    {
        return m_from;
    }

    /// Target point of the current segment.
    T to() const
    {
        return m_to;
    }

    // ─────── Geometric bounds (only for geometric T) ───────

    /**
     * @brief Bounding rectangle covering the full animation path
     *        including curve overshoot.
     *
     * Union of start and target rects; for curves where
     * `overshoots()` is true, the curve is additionally sampled at
     * 50 points and each sample union'd in. The result is the damage
     * rect the consumer must invalidate so no frame of the animation
     * paints outside an already-invalidated region.
     *
     * Available only on geometric specialisations (`QPointF`,
     * `QSizeF`, `QRectF`). Scalar / colour / transform specialisations
     * do not expose this — see design doc decision E.
     */
    QRectF bounds() const
        requires detail::GeometricValue<T>
    {
        return boundsImpl();
    }

    // ─────── Scalar swept range (only for arithmetic T) ───────

    /**
     * @brief Min / max value the animation sweeps through, including
     *        curve overshoot.
     *
     * Available only on scalar specialisations. Consumers use this
     * for damage tracking or axis range sizing where "bounds as a
     * rect" doesn't make sense (a scalar opacity's swept range is
     * [0.0, 1.0] or with overshoot [-0.02, 1.05]).
     */
    std::pair<T, T> sweptRange() const
        requires detail::ScalarValue<T>
    {
        return sweptRangeImpl();
    }

private:
    static constexpr std::chrono::seconds kSafetyCap{60};

    std::shared_ptr<const Curve> effectiveCurve() const
    {
        if (m_spec.profile.curve) {
            return m_spec.profile.curve;
        }
        // Fallback: default OutCubic bezier. Matches the Phase-2
        // CurveRegistry::create("") fallback and the library-defaults
        // invariant in `Profile::withDefaults`.
        static const std::shared_ptr<const Curve> sFallback = std::make_shared<const Easing>();
        return sFallback;
    }

    // Geometric bounds implementation shared across the three geometric T.
    //
    // Implemented via explicit (minX, minY, maxX, maxY) reduction instead
    // of `QRectF::united()` because Qt treats zero-width / zero-height
    // rects as "empty" and drops them from a union — a QPointF sweep
    // (effectively a degenerate rect at each endpoint) would collapse
    // to a single endpoint if run through united(). min/max over the
    // endpoint-and-sample set is the robust formulation.
    QRectF boundsImpl() const
    {
        qreal minX, minY, maxX, maxY;
        if constexpr (std::same_as<T, QPointF>) {
            minX = std::min(m_from.x(), m_to.x());
            maxX = std::max(m_from.x(), m_to.x());
            minY = std::min(m_from.y(), m_to.y());
            maxY = std::max(m_from.y(), m_to.y());
            sampleOvershoots(minX, minY, maxX, maxY, [this](qreal p) {
                const QPointF s = Interpolate<QPointF>::lerp(m_from, m_to, p);
                return std::tuple<qreal, qreal, qreal, qreal>{s.x(), s.y(), s.x(), s.y()};
            });
        } else if constexpr (std::same_as<T, QSizeF>) {
            // QSizeF's "bounds" is the rect at origin covering both
            // the start size and the target size (plus overshoot
            // samples). That's the damage rect the consumer would
            // invalidate if the sized thing is anchored at the origin.
            minX = 0.0;
            minY = 0.0;
            maxX = std::max(m_from.width(), m_to.width());
            maxY = std::max(m_from.height(), m_to.height());
            sampleOvershoots(minX, minY, maxX, maxY, [this](qreal p) {
                const QSizeF s = Interpolate<QSizeF>::lerp(m_from, m_to, p);
                return std::tuple<qreal, qreal, qreal, qreal>{0.0, 0.0, s.width(), s.height()};
            });
        } else {
            // QRectF — full swept rect including overshoot.
            minX = std::min(m_from.left(), m_to.left());
            minY = std::min(m_from.top(), m_to.top());
            maxX = std::max(m_from.right(), m_to.right());
            maxY = std::max(m_from.bottom(), m_to.bottom());
            sampleOvershoots(minX, minY, maxX, maxY, [this](qreal p) {
                const QRectF s = Interpolate<QRectF>::lerp(m_from, m_to, p);
                return std::tuple<qreal, qreal, qreal, qreal>{s.left(), s.top(), s.right(), s.bottom()};
            });
        }
        return QRectF(minX, minY, maxX - minX, maxY - minY);
    }

    template<typename Sampler>
    void sampleOvershoots(qreal& minX, qreal& minY, qreal& maxX, qreal& maxY, const Sampler& sampleAt) const
    {
        const auto curve = effectiveCurve();
        if (!curve || !curve->overshoots()) {
            return;
        }
        // 50 samples — same cadence as Phase 2's AnimationMath::repaintBounds.
        // Overshoot-covering is an upper bound on damage, not a physics
        // simulation; 50 samples hits the peaks of elastic / bounce /
        // underdamped-spring curves within sub-pixel tolerance.
        constexpr int nSamples = 50;
        for (int i = 1; i < nSamples; ++i) {
            const qreal p = curve->evaluate(qreal(i) / nSamples);
            const auto [x1, y1, x2, y2] = sampleAt(p);
            minX = std::min(minX, x1);
            minY = std::min(minY, y1);
            maxX = std::max(maxX, x2);
            maxY = std::max(maxY, y2);
        }
    }

    std::pair<T, T> sweptRangeImpl() const
    {
        T lo = std::min(m_from, m_to);
        T hi = std::max(m_from, m_to);
        const auto curve = effectiveCurve();
        if (curve && curve->overshoots()) {
            constexpr int nSamples = 50;
            for (int i = 1; i < nSamples; ++i) {
                const qreal p = curve->evaluate(qreal(i) / nSamples);
                const T sampled = Interpolate<T>::lerp(m_from, m_to, p);
                lo = std::min(lo, sampled);
                hi = std::max(hi, sampled);
            }
        }
        return {lo, hi};
    }

    // State
    T m_from{};
    T m_to{};
    T m_current{};
    CurveState m_state;
    MotionSpec<T> m_spec;
    std::optional<std::chrono::nanoseconds> m_startTime;
    std::optional<std::chrono::nanoseconds> m_lastTickTime;
    bool m_isAnimating = false;
    bool m_isComplete = false;
};

} // namespace PhosphorAnimation
