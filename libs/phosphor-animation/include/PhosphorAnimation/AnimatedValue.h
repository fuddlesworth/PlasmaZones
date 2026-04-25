// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/Interpolate.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/RetargetPolicy.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QColor>
#include <QLoggingCategory>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace PhosphorAnimation {

// Cross-library export: AnimatedValue<T> is a header-only template, so
// the category is referenced from consumer translation units but defined
// in phosphor-animation's animatedvalue.cpp. Must be exported so the
// symbol resolves across the SO boundary.
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(lcAnimatedValue, PHOSPHORANIMATION_EXPORT)

// NB: Interpolate<T>, ColorSpace, detail:: helpers, PositionalGeometric /
// SizeGeometric / ScalarValue concepts, and kRectSizeEpsilonPx are all
// defined in Interpolate.h (included above). AnimatedValue<T> is the
// stateful progression primitive; the math library it consumes lives
// in Interpolate.h so consumers who need only the lerp/distance helpers
// (tests, direct-draw paths) can include that header without pulling
// in IMotionClock / Profile / Curve / std::chrono.

// Shared fallback curve used by every AnimatedValue<T> instantiation when
// no explicit curve is set on the MotionSpec's Profile. Namespace-scope
// (not a template-local static) so all instantiations share a single
// default instead of leaking one per T. Matches Phase-2 CurveRegistry's
// "empty spec → default OutCubic" semantics and the library-defaults
// invariant in Profile::withDefaults.
PHOSPHORANIMATION_EXPORT std::shared_ptr<const Curve> defaultFallbackCurve();

// Minimum `newDistance` for `PreserveVelocity` retarget on stateful curves.
// Below this threshold the rescale `(velocity * oldDistance / newDistance)`
// either produces a physically meaningless velocity (sub-unit motion) or
// risks overflow — callers retargeting to a distance smaller than this
// auto-degrade to `PreservePosition` (velocity=0). The actual epsilon is
// read from `Interpolate<T>::retargetEpsilon`, which is tuned per domain:
//   - pixel types (QPointF/QSizeF/QRectF/QTransform): 0.5 px
//   - scalar (qreal): 1e-6 (opacity/uniform-friendly)
//   - QColor: 1e-4 (linear-RGB distance scale)
// See each `Interpolate<T>` specialisation for the rationale. The old
// namespace-scope `kRetargetDistanceEpsilon` was a pixel-centric constant
// that quietly swallowed every opacity / colour retarget — per-type
// constants close that hazard while keeping the gate physics-correct for
// the pixel-scale snap path.

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
 *   exceeds a 60-second safety cap (chosen as 2× `Spring`'s internal
 *   `MaxSettleSeconds = 30s` so a spring driven at the edge of its
 *   own settle budget still terminates here under frame-drop-heavy
 *   paint cycles where `dt` accumulation runs behind wall time).
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
template<typename T, ColorSpace Space = ColorSpace::Linear>
class AnimatedValue
{
public:
    AnimatedValue() = default;
    ~AnimatedValue() = default;

    AnimatedValue(const AnimatedValue&) = delete;
    AnimatedValue& operator=(const AnimatedValue&) = delete;
    AnimatedValue(AnimatedValue&&) noexcept = default;
    AnimatedValue& operator=(AnimatedValue&&) noexcept = default;

    // Sibling Space instantiations are friends so `seedFrom` can copy
    // private idle state (m_from, m_to, m_current, m_isComplete) across
    // a space boundary without a public setter per field. Used by
    // wrappers (PhosphorAnimatedColor) that keep one instance per
    // ColorSpace and need to propagate the quiesced visual state when
    // the active space flips.
    template<typename, ColorSpace>
    friend class AnimatedValue;

    // The Space template parameter is only consulted when T == QColor.
    // For every other T the value is ignored and the default Linear is
    // harmless noise in the type. Enforced via `if constexpr` in the
    // lerp dispatch below — no separate partial specialisation is needed.

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
        // NaN/Inf gate. A corrupt `from`/`to` (a settings reload that
        // bypassed clamp, a computed geometry that divided by zero) would
        // propagate non-finite values into `Interpolate<T>::lerp` and
        // poison every downstream paint. `qFuzzyIsNull(distance)` does
        // NOT reject this — distance on NaN inputs returns NaN, and the
        // fuzzy-null comparison returns false. The finite gate has to
        // be explicit at the entry point; per-type `isFinite` handles the
        // componentwise check.
        if (!Interpolate<T>::isFinite(from) || !Interpolate<T>::isFinite(to)) {
            qCWarning(lcAnimatedValue) << "start() rejected: non-finite from/to";
            return false;
        }

        m_from = std::move(from);
        m_to = std::move(to);
        m_spec = std::move(spec);
        m_current = m_from;
        // Resolve and cache the effective curve once per spec. The
        // curve pointer is immutable for the life of this segment
        // (retarget preserves the spec) so we can pay the
        // shared_ptr-copy + fallback-lookup cost one time and read
        // m_cachedCurve on every advance / bounds sample.
        m_cachedCurve = m_spec.profile.curve ? m_spec.profile.curve : defaultFallbackCurve();
        m_state = CurveState{};
        m_state.startValue = 0.0;
        m_state.duration = 1.0; // normalised; real time comes from clock dt
        m_startTime.reset();
        m_lastTickTime.reset();
        m_loggedStatelessDegrade = false;
        m_loggedNegativeDt = false;
        m_loggedTransformDegrade = false;
        m_loggedEpochMismatch = false;

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
     * Returns `true` on acceptance (new segment installed); `false`
     * on two distinct rejections distinguishable by `isComplete()`:
     *
     *   - **No stored spec** (`!isComplete()` post-call): retarget
     *     was called without a prior successful `start()`.
     *   - **Degenerate target** (`isComplete()` post-call): new
     *     segment's distance is ≈ 0, motion is complete-in-place. No
     *     spec callbacks fire (symmetric with `start()`'s degenerate
     *     path); the AnimatedValue is silently marked complete and
     *     the owning container is expected to reap.
     *
     * ## Retarget before the first `advance()`
     *
     * Legal. Calling `retarget()` between `start()` and the first
     * `advance()` leaves `m_startTime` unset (no first-tick latch has
     * happened yet); the reset below (`m_startTime.reset()`) is then
     * a no-op on an already-unset optional. `m_current` still equals
     * `m_from` from `start()`, so the new segment runs from the
     * original start value to the new target — exactly as if
     * `start(m_from, newTo, spec)` had been called instead. The
     * stateful `state.velocity` field in this scenario is whatever
     * the curve was initialised with (zero by default); the
     * PreserveVelocity rescale produces zero velocity through either
     * the `oldDistance > epsilon` gate failing (segment hasn't moved)
     * or the multiplication by zero if the gate were removed —
     * physically correct because there is no in-flight motion to
     * preserve.
     */
    bool retarget(T newTo, RetargetPolicy policy)
    {
        if (!m_spec.clock) {
            qCWarning(lcAnimatedValue) << "retarget() rejected: no stored spec (never started)";
            return false;
        }
        // NaN/Inf gate. Symmetric with `start()` — a corrupt retarget
        // target (settings-reload races, degenerate layout math) must not
        // poison `m_to` and propagate through the next `Interpolate<T>::
        // lerp`. `m_current` is trusted because it was produced by a
        // previous lerp from finite endpoints, but `newTo` comes from the
        // caller and needs the same entry-point gate as `start()`.
        if (!Interpolate<T>::isFinite(newTo)) {
            qCWarning(lcAnimatedValue) << "retarget() rejected: non-finite newTo";
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
            if constexpr (std::same_as<T, QTransform>) {
                // QTransform's distance metric is a Frobenius norm over
                // translate + linear-part components — the units mix
                // (pixels vs. radians vs. scale-factors). Velocity
                // rescale via (vel * oldDist / newDist) is only
                // physically meaningful when both segments are pure
                // translate (linear part is identity on all four
                // endpoints). Otherwise the rescaled velocity is a
                // dimensionless artefact — auto-degrade to
                // PreservePosition and log once per instance.
                const bool pureTranslate = detail::isPureTranslate(m_from) && detail::isPureTranslate(m_to)
                    && detail::isPureTranslate(newFrom) && detail::isPureTranslate(newTo);
                if (!pureTranslate) {
                    if (!m_loggedTransformDegrade) {
                        qCDebug(lcAnimatedValue) << "QTransform PreserveVelocity degrading to PreservePosition: "
                                                 << "non-translate components present (Frobenius metric mixes units)";
                        m_loggedTransformDegrade = true;
                    }
                    newVelocity = 0.0;
                    break;
                }
            }
            if (stateful && newDistance > Interpolate<T>::retargetEpsilon
                && oldDistance > Interpolate<T>::retargetEpsilon) {
                // Map scalar normalised-velocity through the world:
                //   worldRate [T-units/s] = state.velocity [1/s] * oldDistance [T-units]
                //   newNormalisedVelocity [1/s] = worldRate / newDistance
                // For vector T, distance() returns magnitude — direction
                // is lost (documented limitation; see header intro).
                //
                // Per-type epsilon (`Interpolate<T>::retargetEpsilon`) gates
                // BOTH distances:
                //
                // - `newDistance` gate: a sub-threshold target would let
                //   `oldDistance / newDistance` explode on a drag-snap or
                //   fade-retarget workflow that lands the new target
                //   arbitrarily close to the current visual value. A runaway
                //   `newVelocity` pumps the spring into oscillation or a
                //   force-complete clamp on the next tick.
                //
                // - `oldDistance` gate: a retarget fired at t=0 of a
                //   segment (before the first advance has moved m_current
                //   away from m_from) has oldDistance ≈ 0 by construction,
                //   which collapses the rescaled velocity to 0 implicitly
                //   via the multiply. Making the gate explicit symmetrises
                //   the two distance checks and documents that a
                //   zero-length source segment has no physical velocity to
                //   preserve regardless of the stored `state.velocity` —
                //   if the curve hasn't moved, there's no world-rate to
                //   project forward even if the scalar velocity field is
                //   non-zero (e.g., a retarget issued immediately after
                //   setting initial velocity on a stateful curve that
                //   hasn't stepped yet).
                //
                // Below either epsilon the velocity-preserving rescale is
                // not physically meaningful — degrade to PreservePosition
                // (velocity=0) so the spring settles smoothly. The
                // threshold scales per type: 0.5 px for pixel-coordinate
                // T, tighter for scalar / colour domains whose natural
                // range is [0, 1]-ish.
                newVelocity = (m_state.velocity * oldDistance) / newDistance;
            } else if (!stateful && !m_loggedStatelessDegrade) {
                // One-shot per-instance. Logging every retarget on a
                // stateless curve would flood logs under drag-snap
                // workflows (one retarget per cursor frame).
                qCDebug(lcAnimatedValue) << "PreserveVelocity degrading to PreservePosition on stateless curve"
                                         << (curve ? curve->typeId() : QStringLiteral("null"));
                m_loggedStatelessDegrade = true;
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
            // complete-in-place, no motion. The degenerate path is
            // silent: no spec callbacks fire, matching start()'s
            // degenerate-path silence.
            //
            // Rationale: the spec callbacks ultimately feed into the
            // owning controller's lifecycle accounting. Firing
            // onComplete here and then having the controller's own
            // onAnimationComplete fire on the next advance would
            // double-count terminal events against one started
            // animation. Callers observe the degenerate path via
            // (retarget() returning false && isComplete() == true)
            // and let the controller reap on the next tick — or the
            // controller detects the zombie immediately and reaps
            // from within its retarget() dispatcher.
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

    /// Swap the driving clock without touching target/state/interpolation.
    /// Definition (with the full epoch-rebase contract docs) lives in
    /// <PhosphorAnimation/AnimatedValue_lifecycle_extras.h>.
    void rebindClock(IMotionClock* newClock);

    /**
     * @brief Stop animating; leave `value()` at its current position.
     *
     * Does not fire `onComplete` — cancellation is explicitly non-
     * completion. Consumers that need cleanup on both paths observe
     * cancellation via their own logic (e.g., after calling cancel()
     * the caller knows the animation was interrupted; onComplete
     * exists for natural termination only).
     *
     * ## Owning-container stranding
     *
     * `cancel()` leaves `isAnimating()` AND `isComplete()` both false.
     * An `AnimatedValue` held inside an owning container
     * (`AnimationController::m_animations`, the QML driver's handle
     * map, …) is NOT self-reaping after cancel — the owner must
     * explicitly remove the entry (e.g.,
     * `AnimationController::removeAnimation(handle)`), otherwise it
     * sticks around until the container is cleared. `AnimationController`
     * never calls `cancel()` on a held entry for this reason — its
     * re-target / replace flows either call `retarget()` (installs a
     * new segment) or `startAnimation()` (drops+restarts with
     * `onAnimationReplaced`).
     */
    void cancel()
    {
        m_isAnimating = false;
        m_isComplete = false;
    }

    /// Cross-Space idle-state copy for color-space wrapper dispatch.
    /// Definition + idle-precondition rationale live in
    /// <PhosphorAnimation/AnimatedValue_lifecycle_extras.h>.
    template<ColorSpace OtherSpace>
    void seedFrom(const AnimatedValue<T, OtherSpace>& other);

    /// Companion to seedFrom — copies clock + callbacks, leaves profile alone.
    /// Definition + rationale live in
    /// <PhosphorAnimation/AnimatedValue_lifecycle_extras.h>.
    template<ColorSpace OtherSpace>
    void seedSpecFrom(const AnimatedValue<T, OtherSpace>& other);

    /**
     * @brief Force the animation to its target value immediately.
     *
     * Snaps `value()` to target, marks complete, and fires both
     * `onValueChanged(target)` (once, with the final value) and
     * `onComplete` (exactly once). Intermediate per-tick
     * `onValueChanged` ticks are skipped — consumers that accumulate
     * per-tick state should treat `finish()` as a terminal jump, not
     * a continued progression.
     *
     * Used by consumers that want to collapse an in-flight animation
     * (e.g., window minimize that interrupts an in-flight snap — the
     * final geometry is what matters, not the interpolation).
     */
    void finish()
    {
        if (!m_isAnimating) {
            // Never started (isAnimating=false, isComplete=false) OR
            // already naturally-completed (isAnimating=false,
            // isComplete=true). Both paths are idempotent no-ops —
            // start() hasn't been called or the completion callbacks
            // have already fired and must not re-fire.
            return;
        }
        // m_isAnimating is true from here on. isComplete cannot be
        // true simultaneously — advance() clears isAnimating when it
        // sets isComplete. So no further guard is needed before the
        // terminal-state writes below.
        m_current = m_to;
        m_state.value = 1.0;
        m_state.velocity = 0.0;
        m_isAnimating = false;
        m_isComplete = true;
        if (m_spec.onValueChanged) {
            m_spec.onValueChanged(m_current);
        }
        // Re-entrancy guard: if onValueChanged restarted this AnimatedValue
        // via move-assignment (controller clobber), m_isComplete would
        // now be false for the new segment — firing onComplete on the new
        // spec would signal completion of an animation that just began.
        if (m_isComplete && m_spec.onComplete) {
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
     *
     * ## Callback lifetime contract (important)
     *
     * Spec callbacks (`onValueChanged`, `onComplete`) must NOT cause
     * `*this` to be destroyed while they run. Concretely, a callback
     * must not call `container.erase(handle)` / equivalent on the
     * AnimatedValue currently firing — doing so destroys `*this`
     * under the call stack of `advance()`, and subsequent code inside
     * `advance()` (or its caller, if the caller re-inspects `this`)
     * is use-after-free.
     *
     * In-place restart via `this->start(newFrom, newTo, newSpec)` is
     * safe: the re-entrancy guard re-checks `m_isComplete` after
     * `onValueChanged` fires and suppresses the old `onComplete` when
     * a new segment has been installed.
     *
     * `AnimationController` honours this contract — its own
     * `onAnimationComplete` hook fires on a locally-moved copy *after*
     * the controller has erased the entry, so callers wiring that hook
     * are free to erase / restart other handles. The contract only
     * restricts what spec-level callbacks (attached via `MotionSpec`)
     * may do.
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
            // Callback may have re-entered — re-read m_spec.clock
            // rather than caching pre-callback.
            if (m_spec.clock) {
                m_spec.clock->requestFrame();
            }
            return;
        }

        const auto elapsed = now - *m_startTime;
        const qreal dtSeconds = std::chrono::duration<qreal>(now - *m_lastTickTime).count();
        m_lastTickTime = now;

        // Defensive: negative dt indicates a non-monotonic clock
        // (IMotionClock mandates monotonicity; this is belt-and-
        // suspenders). Treat as zero-step and request another frame.
        // Log once per instance so a misbehaving clock is visible in
        // diagnostics without flooding at paint rate.
        if (dtSeconds < 0.0) {
            if (!m_loggedNegativeDt) {
                qCWarning(lcAnimatedValue) << "negative dt from clock (" << dtSeconds
                                           << "s) — treating as zero-step. Monotonicity contract violated.";
                m_loggedNegativeDt = true;
            }
            m_spec.clock->requestFrame();
            return;
        }

        const auto curve = effectiveCurve();

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
            // Parametric progression: t = elapsed / duration. elapsedMs
            // + duration are only consulted on the stateless branch;
            // keep the computation local to avoid per-tick work on the
            // stateful path.
            const qreal durationMs = m_spec.profile.effectiveDuration();
            const qreal elapsedMs = std::chrono::duration<qreal, std::milli>(elapsed).count();
            // `std::isfinite` gate covers NaN/Inf: a corrupt Profile that
            // landed a non-finite duration (settings reload wrote NaN
            // before the central clamp caught it, or a future code path
            // computes duration from user input without guarding) would
            // otherwise divide `elapsedMs / NaN` → NaN → poison
            // `state.value` and every subsequent paint. Treat non-finite
            // and non-positive durations identically — terminal-frame
            // snap to exactly 1.0.
            if (!std::isfinite(durationMs) || durationMs <= 0.0 || elapsedMs >= durationMs) {
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
            // Re-entrancy guard: if onValueChanged restarted this
            // AnimatedValue in-place (user callback re-enters the
            // owning controller and triggers insert_or_assign on the
            // same handle), m_isComplete would have been reset to
            // false by the new spec's start(). Firing the NEW spec's
            // onComplete at that point would signal completion for an
            // animation that just began. Skip when that happens —
            // the old animation's onComplete is effectively replaced
            // by the new animation's lifecycle.
            if (m_isComplete && m_spec.onComplete) {
                m_spec.onComplete();
            }
            // No clock->requestFrame() here — completion means no
            // further ticks are needed. The consumer's completion
            // callback can still schedule its own final paint.
        } else {
            m_current = lerpStateValue();
            if (m_spec.onValueChanged) {
                m_spec.onValueChanged(m_current);
            }
            // Callback may have re-entered and swapped m_spec — re-read
            // m_spec.clock rather than caching it pre-callback, so we
            // request the NEXT frame on whichever driver is now live.
            if (m_spec.clock) {
                m_spec.clock->requestFrame();
            }
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

    // ─────── Geometric bounds & swept-range queries ───────
    //
    // Definitions live in <PhosphorAnimation/AnimatedValue_geometric.h>,
    // included unconditionally at the bottom of this file. The split
    // exists because the combined header crossed the project's 800-line
    // cap; the geometric surface is logically separable (snap engine
    // never queries bounds on its own animations, and damage callers
    // never need start()) so the cut preserves cohesion. ABI-compatible
    // — every consumer keeps including only <PhosphorAnimation/
    // AnimatedValue.h>. See the sibling header for full per-method docs.

    /// Bounding rectangle of the swept path including curve overshoot.
    QRectF bounds() const
        requires detail::PositionalGeometric<T>;

    /// Damage rect anchored at @p anchor — size-only T (QSizeF).
    QRectF boundsAt(QPointF anchor) const
        requires detail::SizeGeometric<T>;

    /// (minSize, maxSize) the animation sweeps through — QSizeF only.
    std::pair<QSizeF, QSizeF> sweptSize() const
        requires detail::SizeGeometric<T>;

    /// True when current size diverges from target by > @p epsilonPx.
    bool hasSizeChange(qreal epsilonPx = kRectSizeEpsilonPx) const
        requires std::same_as<T, QRectF>;

    /// (lo, hi) of a scalar animation's swept range, with overshoot.
    std::pair<T, T> sweptRange() const
        requires detail::ScalarValue<T>;

    /**
     * @brief Terminal wall-clock guard for stateful curves.
     *
     * Any stateful progression still running after this many seconds
     * is force-completed (see `advance()`). Public so tests can assert
     * against the exact boundary instead of hard-coding "61 s" and
     * silently re-interpreting the failure mode if the cap moves.
     */
    static constexpr std::chrono::seconds safetyCap() noexcept
    {
        return kSafetyCap;
    }

private:
    static constexpr std::chrono::seconds kSafetyCap{60};

    // Overshoot sampling cadence — same across every bounds /
    // swept-range query. 50 samples hits the peaks of elastic /
    // bounce / underdamped-spring curves within sub-pixel tolerance
    // (matches Phase 2's AnimationMath::repaintBounds cadence).
    static constexpr int kOvershootSamples = 50;

    // Colour-space aware lerp dispatch. For T == QColor with
    // Space == OkLab, routes through the OkLab conversion path;
    // for every other (T, Space) pair, falls through to the standard
    // Interpolate<T>::lerp specialisation. `if constexpr` keeps the
    // dispatch compile-time zero-cost — no runtime branch survives
    // in the emitted code for any concrete instantiation.
    T lerpStateValue() const
    {
        if constexpr (std::same_as<T, QColor> && Space == ColorSpace::OkLab) {
            return detail::lerpColorOkLab(m_from, m_to, m_state.value);
        } else {
            return Interpolate<T>::lerp(m_from, m_to, m_state.value);
        }
    }

    std::shared_ptr<const Curve> effectiveCurve() const
    {
        // Cached in start(); fallback lookup only runs for the
        // default-constructed (never-started) case — avoids the
        // shared_ptr copy + Meyers-singleton load on every advance.
        if (m_cachedCurve) {
            return m_cachedCurve;
        }
        return defaultFallbackCurve();
    }

    // ─── Geometric impl helpers (defined in AnimatedValue_geometric.h) ───
    // Forward declarations only — bodies live alongside the public
    // geometric API in the sibling header so the file-size split keeps
    // both the public surface and its impl together for review.

    QRectF boundsImpl() const
        requires detail::PositionalGeometric<T>;

    std::pair<QSizeF, QSizeF> sweptSizeImpl() const
        requires detail::SizeGeometric<T>;

    template<typename Sampler>
    void sampleOvershoots(qreal& minX, qreal& minY, qreal& maxX, qreal& maxY, const Sampler& sampleAt) const;

    std::pair<T, T> sweptRangeImpl() const;

    // State
    T m_from{};
    T m_to{};
    T m_current{};
    CurveState m_state;
    MotionSpec<T> m_spec;
    // Resolved curve pointer — cached in start() so the per-advance
    // path does not pay a shared_ptr copy + fallback lookup. Empty for
    // a default-constructed (never-started) instance; effectiveCurve()
    // falls back to defaultFallbackCurve() in that case.
    std::shared_ptr<const Curve> m_cachedCurve;
    std::optional<std::chrono::nanoseconds> m_startTime;
    std::optional<std::chrono::nanoseconds> m_lastTickTime;
    bool m_isAnimating = false;
    bool m_isComplete = false;
    // Rate-limit: set once we've logged the PreserveVelocity→
    // PreservePosition degrade on a stateless curve, to avoid flooding
    // logs when a consumer retargets every frame (drag-snap).
    bool m_loggedStatelessDegrade = false;
    // Rate-limit: set once we've observed and logged a negative dt from
    // the clock (monotonicity violation). Same reasoning — drag-snap
    // workflows would flood logs otherwise.
    bool m_loggedNegativeDt = false;
    // Rate-limit: set once we've auto-degraded a PreserveVelocity
    // retarget on a non-pure-translate QTransform animation. Unused for
    // every other T (the field is cheap and avoids the complexity of a
    // conditional specialization).
    bool m_loggedTransformDegrade = false;
    // Rate-limit: set once we've refused a rebindClock across
    // incompatible clock epochs. Same rationale as the other flags —
    // a consumer routing migrations every frame would otherwise flood.
    bool m_loggedEpochMismatch = false;
};

} // namespace PhosphorAnimation

// Out-of-class member-template definitions split into sibling headers to
// keep this file under the project's 800-line cap. MUST be included AFTER
// the class is fully defined and AFTER the namespace closes (each sibling
// re-opens its own namespace block). Header-only and ABI-compatible —
// every consumer keeps using `#include <PhosphorAnimation/AnimatedValue.h>`
// and transparently picks up every part.
#include <PhosphorAnimation/AnimatedValue_geometric.h>
#include <PhosphorAnimation/AnimatedValue_lifecycle_extras.h>
