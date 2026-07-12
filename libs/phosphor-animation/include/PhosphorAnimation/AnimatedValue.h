// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimationLimits.h>
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

// Export needed because the category is referenced from consumer TUs
// but defined in phosphor-animation's animatedvalue.cpp.
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(lcAnimatedValue, PHOSPHORANIMATION_EXPORT)

PHOSPHORANIMATION_EXPORT std::shared_ptr<const Curve> defaultFallbackCurve();

/// Unified motion primitive: one value of type T transitioning from start
/// to target over time, driven by a curve and a clock. Move-only.
/// GUI-thread only (matches IMotionClock's contract).
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

    // Sibling Space instantiations are friends so seedFrom can copy
    // private idle state across a space boundary.
    template<typename, ColorSpace>
    friend class AnimatedValue;

    /// Begin an animation. Returns false if degenerate (from == to) or invalid.
    bool start(T from, T to, MotionSpec<T> spec)
    {
        if (!spec.clock) {
            qCWarning(lcAnimatedValue) << "start() rejected: null clock";
            return false;
        }
        // NaN/Inf gate — corrupt endpoints would poison every downstream paint.
        if (!Interpolate<T>::isFinite(from) || !Interpolate<T>::isFinite(to)) {
            qCWarning(lcAnimatedValue) << "start() rejected: non-finite from/to";
            return false;
        }

        m_from = std::move(from);
        m_to = std::move(to);
        m_spec = std::move(spec);
        m_current = m_from;
        m_cachedCurve = m_spec.profile.curve ? m_spec.profile.curve : defaultFallbackCurve();
        m_state = CurveState{};
        m_state.startValue = 0.0;
        m_state.duration = 1.0;
        m_startTime.reset();
        m_lastTickTime.reset();
        m_loggedStatelessDegrade = false;
        m_loggedNegativeDt = false;
        m_loggedTransformDegrade = false;
        m_loggedEpochMismatch = false;

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

    /// Redirect in-flight animation to a new target. Returns false on
    /// rejection (no stored spec) or degenerate target (complete-in-place).
    bool retarget(T newTo, RetargetPolicy policy)
    {
        if (!m_spec.clock) {
            qCWarning(lcAnimatedValue) << "retarget() rejected: no stored spec (never started)";
            return false;
        }
        if (!Interpolate<T>::isFinite(newTo)) {
            qCWarning(lcAnimatedValue) << "retarget() rejected: non-finite newTo";
            return false;
        }

        const T newFrom = m_current;
        const qreal oldDistance = Interpolate<T>::distance(m_from, m_to);
        const qreal newDistance = Interpolate<T>::distance(newFrom, newTo);

        const auto curve = effectiveCurve();
        const bool stateful = curve && curve->isStateful();

        qreal newVelocity = 0.0;
        switch (policy) {
        case RetargetPolicy::PreserveVelocity:
            if constexpr (std::same_as<T, QTransform>) {
                // Frobenius metric mixes units for non-translate transforms —
                // velocity rescale is only meaningful for pure-translate segments.
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
                // Map normalised velocity through world-space:
                //   worldRate = state.velocity * oldDistance
                //   newNormalisedVelocity = worldRate / newDistance
                // Per-type epsilon gates BOTH distances to prevent
                // velocity explosion on sub-threshold retargets.
                newVelocity = (m_state.velocity * oldDistance) / newDistance;
            } else if (!stateful && !m_loggedStatelessDegrade) {
                qCDebug(lcAnimatedValue) << "PreserveVelocity degrading to PreservePosition on stateless curve"
                                         << (curve ? curve->typeId() : QStringLiteral("null"));
                m_loggedStatelessDegrade = true;
            }
            break;
        case RetargetPolicy::ResetVelocity:
            newVelocity = 0.0;
            break;
        case RetargetPolicy::PreservePosition:
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

    /// Convenience overload using m_spec.retargetPolicy.
    bool retarget(T newTo)
    {
        return retarget(std::move(newTo), m_spec.retargetPolicy);
    }

    /// Swap the driving clock without touching target/state/interpolation.
    void rebindClock(IMotionClock* newClock);

    /// Stop animating; leave value() at its current position.
    /// Does not fire onComplete — cancellation is non-completion.
    /// Leaves isAnimating() AND isComplete() both false.
    void cancel()
    {
        m_isAnimating = false;
        m_isComplete = false;
    }

    /// Cross-Space idle-state copy for color-space wrapper dispatch.
    template<ColorSpace OtherSpace>
    void seedFrom(const AnimatedValue<T, OtherSpace>& other);

    /// Companion to seedFrom — copies clock + callbacks, leaves profile alone.
    template<ColorSpace OtherSpace>
    void seedSpecFrom(const AnimatedValue<T, OtherSpace>& other);

    /// Snap to target immediately, fire onValueChanged + onComplete.
    void finish()
    {
        if (!m_isAnimating) {
            return;
        }
        m_current = m_to;
        m_state.value = 1.0;
        m_state.velocity = 0.0;
        m_isAnimating = false;
        m_isComplete = true;
        if (m_spec.onValueChanged) {
            m_spec.onValueChanged(m_current);
        }
        // Re-entrancy guard: onValueChanged may have restarted this instance.
        if (m_isComplete && m_spec.onComplete) {
            m_spec.onComplete();
        }
    }

    /// Advance animation by one paint tick. No-op when not animating.
    /// Spec callbacks must NOT destroy *this while running (see header).
    void advance()
    {
        if (!m_isAnimating || !m_spec.clock) {
            return;
        }

        const auto now = m_spec.clock->now();

        if (!m_startTime) {
            m_startTime = now;
            m_lastTickTime = now;
            m_current = m_from;
            if (m_spec.onValueChanged) {
                m_spec.onValueChanged(m_current);
            }
            if (m_spec.clock) {
                m_spec.clock->requestFrame();
            }
            return;
        }

        const auto elapsed = now - *m_startTime;
        const qreal dtSeconds = std::chrono::duration<qreal>(now - *m_lastTickTime).count();
        m_lastTickTime = now;

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
            // Cap the integrator step at Limits::MaxShaderTimeDeltaSeconds.
            // Spring::step is semi-implicit Euler, so an unbounded step diverges:
            // a suspend/resume, GC pause, or scheduler stall would otherwise hand
            // it a multi-second dt in a single tick, blow the velocity up, and
            // leave the value garbage. kSafetyCap below is no defence — it tests
            // ELAPSED and runs AFTER the step, so it forces completion on an
            // already-corrupt state rather than preventing it. The cap bounds the
            // blast radius; it does not make the integrator unconditionally
            // stable (strict stability wants dt < 1/(5*omega), and Spring admits
            // omega up to 200). Substepping would be the real fix for a stiff
            // spring.
            //
            // Only this branch integrates dt — the parametric branch derives its
            // value from elapsed/duration, so a stall there merely lands further
            // along the curve, which is correct. A clamped spring advances less
            // than wall-clock across a stall (it "skips" less), and kSafetyCap
            // still bounds its lifetime.
            curve->step(qMin(dtSeconds, static_cast<qreal>(Limits::MaxShaderTimeDeltaSeconds)), m_state, 1.0);

            // A stateful curve's lifetime is its own analytical settle time. The
            // convergence test below is necessary but NOT sufficient: it wants
            // |velocity| <= 1e-6, which an UNDAMPED spring (zeta = 0, inside
            // Spring's own qBound(0, zeta, 10) and reachable from the wire string
            // "spring:12,0") never satisfies — it oscillates forever, so without
            // this bound it would run to kSafetyCap, pinning per-frame repaints
            // for a full minute. Even a merely low-damping spring overshoots the
            // envelope: "spring:1,0.2" converges only after ~46 s.
            //
            // settleTime() is the curve's own definition of "done" (bounded by
            // Spring::MaxSettleSeconds), so this is a physics bound, not a policy
            // one — it stays correct for consumers outside the compositor's
            // duration envelope, such as the daemon's SurfaceAnimator. It is also
            // the SAME rule ShaderInternal::resolveTransitionLifetimeMs applies on
            // the shader side, so the geometry leg and the shader leg of one curve
            // now agree on how long they run instead of diverging (they differed
            // by 2.35x for the default spring: 0.41 s vs the 1e-4 lock at 0.96 s).
            const qreal elapsedMs = std::chrono::duration<qreal, std::milli>(elapsed).count();
            const qreal settleMs = curve->settleTime() * 1000.0;
            if (m_state.value >= 1.0 && qAbs(m_state.velocity) <= 1.0e-6) {
                complete = true;
            } else if (std::isfinite(settleMs) && elapsedMs >= settleMs) {
                complete = true;
            } else if (elapsed > kSafetyCap) {
                qCWarning(lcAnimatedValue) << "stateful curve exceeded safety cap; forcing completion";
                complete = true;
            }
        } else {
            const qreal durationMs = m_spec.profile.effectiveDuration();
            const qreal elapsedMs = std::chrono::duration<qreal, std::milli>(elapsed).count();
            if (!std::isfinite(durationMs) || durationMs <= 0.0 || elapsedMs >= durationMs) {
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
            // Re-entrancy guard: onValueChanged may have restarted via start().
            if (m_isComplete && m_spec.onComplete) {
                m_spec.onComplete();
            }
        } else {
            m_current = lerpStateValue();
            if (m_spec.onValueChanged) {
                m_spec.onValueChanged(m_current);
            }
            if (m_spec.clock) {
                m_spec.clock->requestFrame();
            }
        }
    }

    T value() const
    {
        return m_current;
    }
    qreal velocity() const
    {
        return m_state.velocity;
    }
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
    const MotionSpec<T>& spec() const
    {
        return m_spec;
    }
    T from() const
    {
        return m_from;
    }
    T to() const
    {
        return m_to;
    }

    // Geometric bounds & swept-range queries — definitions in
    // AnimatedValue_geometric.h, included at the bottom of this file.

    QRectF bounds() const
        requires detail::PositionalGeometric<T>;

    QRectF boundsAt(QPointF anchor) const
        requires detail::SizeGeometric<T>;

    std::pair<QSizeF, QSizeF> sweptSize() const
        requires detail::SizeGeometric<T>;

    bool hasSizeChange(qreal epsilonPx = kRectSizeEpsilonPx) const
        requires std::same_as<T, QRectF>;

    std::pair<T, T> sweptRange() const
        requires detail::ScalarValue<T>;

    static constexpr std::chrono::seconds safetyCap() noexcept
    {
        return kSafetyCap;
    }

private:
    static constexpr std::chrono::seconds kSafetyCap{60};
    static constexpr int kOvershootSamples = 50;

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
        if (m_cachedCurve) {
            return m_cachedCurve;
        }
        return defaultFallbackCurve();
    }

    QRectF boundsImpl() const
        requires detail::PositionalGeometric<T>;

    std::pair<QSizeF, QSizeF> sweptSizeImpl() const
        requires detail::SizeGeometric<T>;

    template<typename Sampler>
    void sampleOvershoots(qreal& minX, qreal& minY, qreal& maxX, qreal& maxY, const Sampler& sampleAt) const;

    std::pair<T, T> sweptRangeImpl() const;

    T m_from{};
    T m_to{};
    T m_current{};
    CurveState m_state;
    MotionSpec<T> m_spec;
    std::shared_ptr<const Curve> m_cachedCurve;
    std::optional<std::chrono::nanoseconds> m_startTime;
    std::optional<std::chrono::nanoseconds> m_lastTickTime;
    bool m_isAnimating = false;
    bool m_isComplete = false;
    bool m_loggedStatelessDegrade = false; // rate-limit: stateless PreserveVelocity degrade
    bool m_loggedNegativeDt = false; // rate-limit: non-monotonic clock
    bool m_loggedTransformDegrade = false; // rate-limit: QTransform velocity degrade
    bool m_loggedEpochMismatch = false; // rate-limit: epoch mismatch on rebindClock
};

} // namespace PhosphorAnimation

#include <PhosphorAnimation/AnimatedValue_geometric.h>
#include <PhosphorAnimation/AnimatedValue_lifecycle_extras.h>
