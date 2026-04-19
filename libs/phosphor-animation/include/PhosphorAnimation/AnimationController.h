// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/RetargetPolicy.h>
#include <PhosphorAnimation/SnapPolicy.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QMarginsF>
#include <QRectF>
#include <QVarLengthArray>

#include <memory>
#include <unordered_map>
#include <utility>

namespace PhosphorAnimation {

/**
 * @brief Compositor-agnostic per-window snap-animation controller.
 *
 * Phase 3 rewrite on top of `AnimatedValue<QRectF>`. Owns a
 * `std::unordered_map<Handle, AnimatedValue<QRectF>>`
 * (chosen over QHash because AnimatedValue<T> is move-only; see the
 * m_animations rationale comment), a Profile (curve + duration),
 * a minimum-distance skip threshold, and a non-owning `IMotionClock*`.
 * Subclasses bind the @c Handle template parameter to their
 * compositor's window-handle type (`KWin::EffectWindow*` for KDE,
 * `wlr_surface*` for a future wlroots adapter, an integer id for QML
 * overlays) and override the five virtual hooks to splice the
 * controller into the surrounding paint pipeline.
 *
 * ## What changed from Phase 2
 *
 * - Internal state is `AnimatedValue<QRectF>` instead of `WindowMotion`.
 *   `WindowMotion` and `AnimationMath` are deleted.
 * - `setClock()` is now required — the controller pulls `dt` via the
 *   clock instead of taking `presentTime` as an `advanceAnimations`
 *   parameter. This matches the rest of the Phase 3 motion runtime
 *   (`AnimatedValue<T>`, `IMotionClock`).
 * - Profile bundles duration + curve (previously separate setters).
 *   Convenience setters (`setDuration`, `setCurve`) still exist for
 *   ergonomic config-change callbacks and compose into the stored Profile.
 * - `startAnimation` takes `(QRectF oldFrame, QRectF newFrame)`
 *   instead of split `(QPointF, QSizeF, QRect)`.
 * - New `retarget(Handle, QRectF, RetargetPolicy)` method — the
 *   velocity-preserving redirect that was only possible via
 *   remove+start-with-captured-visual-state in Phase 2.
 *
 * ## Virtual hooks (unchanged from Phase 2)
 *
 * - @ref onAnimationStarted   — fired when @ref startAnimation accepts.
 * - @ref onAnimationComplete  — fired when an animation completes.
 * - @ref onRepaintNeeded      — fired with a bounds rect for damage.
 * - @ref isHandleValid        — reject stale handles.
 * - @ref expandedPadding      — compositor-specific shadow / decoration margin.
 *
 * ## Re-entrancy contract (preserved from Phase 2)
 *
 * The completion hook may call back into the controller —
 * `startAnimation`, `retarget`, `removeAnimation`, `clear` are all
 * safe. The entry for the completing handle is erased *before* the
 * hook fires; handles are snapshotted before iteration, so hook-driven
 * mutation of `m_animations` cannot invalidate the outer loop.
 *
 * ## Thread safety
 *
 * GUI-thread only. Matches `IMotionClock` and `AnimatedValue<T>`.
 */
template<typename Handle>
class AnimationController
{
public:
    AnimationController() = default;
    virtual ~AnimationController() = default;

    AnimationController(const AnimationController&) = delete;
    AnimationController& operator=(const AnimationController&) = delete;

    // ─────── Configuration ───────

    /// The clock that drives every animation started by this controller.
    /// Non-owning; outlives the controller. Must be set before
    /// @ref startAnimation — otherwise the call is rejected.
    void setClock(IMotionClock* clock)
    {
        m_clock = clock;
    }
    IMotionClock* clock() const
    {
        return m_clock;
    }

    void setEnabled(bool enabled)
    {
        m_enabled = enabled;
    }
    bool isEnabled() const
    {
        return m_enabled;
    }

    /// Profile bundling curve + duration (and any future orchestration
    /// fields). Applied to new animations only; in-flight animations
    /// keep their own MotionSpec copy and are unaffected by subsequent
    /// profile swaps — matches Phase 3 decision K (config-reload
    /// immutability).
    void setProfile(const Profile& profile)
    {
        m_profile = profile;
    }
    const Profile& profile() const
    {
        return m_profile;
    }

    /// Convenience setter — equivalent to modifying `profile().curve`.
    /// Kept for call-site ergonomics (Phase 2 consumers update the
    /// curve independently of duration via a dedicated settings
    /// callback).
    void setCurve(std::shared_ptr<const Curve> curve)
    {
        m_profile.curve = std::move(curve);
    }
    std::shared_ptr<const Curve> curve() const
    {
        return m_profile.curve;
    }

    /// Convenience setter — equivalent to modifying `profile().duration`.
    /// Clamps to [0, 10000] ms (same range as Phase 2).
    void setDuration(qreal ms)
    {
        m_profile.duration = qBound(qreal(0.0), ms, qreal(10000.0));
    }
    qreal duration() const
    {
        return m_profile.effectiveDuration();
    }

    /// Minimum translate distance (in px) before a transition is worth
    /// animating. Clamped to [0, 10000]. Callers wanting a tighter
    /// clamp apply it themselves.
    void setMinDistance(int pixels)
    {
        m_minDistance = qBound(0, pixels, 10000);
    }
    int minDistance() const
    {
        return m_minDistance;
    }

    // ─────── Lifecycle ───────

    bool hasActiveAnimations() const
    {
        return !m_animations.empty();
    }

    bool hasAnimation(Handle handle) const
    {
        return m_animations.find(handle) != m_animations.end();
    }

    /**
     * @brief Start an animation from @p oldFrame to @p newFrame.
     *
     * Returns `false` when:
     *   - @ref isEnabled() is false,
     *   - no clock has been set,
     *   - @ref isHandleValid returns false for @p handle,
     *   - @ref SnapPolicy::createSnapSpec rejects the transition
     *     (degenerate target, sub-threshold motion).
     *
     * Re-calling for a handle that already has an in-flight motion
     * replaces it: the displaced animation fires
     * @ref onAnimationReplaced and is dropped without firing
     * @ref onAnimationComplete (it was not naturally completed), then
     * @ref onAnimationStarted fires for the new segment. Adapters that
     * want visual continuity across the boundary should call
     * @ref retarget instead; `startAnimation`'s replace semantics jump
     * visually from the in-flight value to the new `oldFrame`.
     *
     * Otherwise the animation is stored, @ref onAnimationStarted is
     * fired, and the function returns `true`.
     */
    bool startAnimation(Handle handle, const QRectF& oldFrame, const QRectF& newFrame)
    {
        if (!m_enabled) {
            return false;
        }
        if (!m_clock) {
            return false;
        }
        if (!isHandleValid(handle)) {
            return false;
        }

        SnapPolicy::SnapParams params;
        params.profile = m_profile;
        params.minDistance = m_minDistance;
        const auto spec = SnapPolicy::createSnapSpec(oldFrame, newFrame, params, m_clock);
        if (!spec) {
            return false;
        }

        AnimatedValue<QRectF> anim;
        if (!anim.start(oldFrame, newFrame, *spec)) {
            return false;
        }

        // Displace + notify: a naïve insert_or_assign would silently
        // drop the prior animation, leaving consumers that count
        // started-vs-complete events with a stranded "started". Fire
        // onAnimationReplaced on the displaced instance so the event
        // accounting stays balanced (one started ⇒ exactly one
        // terminating event: complete OR replaced OR removed).
        if (auto existing = m_animations.find(handle); existing != m_animations.end()) {
            AnimatedValue<QRectF> displaced = std::move(existing->second);
            m_animations.erase(existing);
            onAnimationReplaced(handle, displaced);
        }

        // unordered_map's operator[] requires default-constructible value
        // and copy-assigns — AnimatedValue<T> is move-only, so use
        // emplace (C++17) which accepts rvalue value.
        auto [it, inserted] = m_animations.emplace(handle, std::move(anim));
        onAnimationStarted(handle, it->second);
        return true;
    }

    /**
     * @brief Redirect an in-flight animation to a new target.
     *
     * Preserves the visible value (new segment starts from current
     * value) and reshapes state per @p policy. Returns `false` when
     * no animation exists for @p handle — in that case the caller
     * should use @ref startAnimation instead.
     *
     * `PreserveVelocity` on a stateful curve (Spring) carries the
     * world-space rate of change across the retarget boundary.
     * On a stateless curve (Easing) it degrades to `PreservePosition`
     * with a debug log (no physical velocity exists to preserve).
     *
     * Fires @ref onAnimationRetargeted on success so adapters that
     * latch per-segment state (damage annotation, telemetry) can
     * observe the new segment without conflating it with a fresh
     * `startAnimation` call.
     */
    bool retarget(Handle handle, const QRectF& newFrame, RetargetPolicy policy = RetargetPolicy::PreserveVelocity)
    {
        auto it = m_animations.find(handle);
        if (it == m_animations.end()) {
            return false;
        }
        const bool accepted = it->second.retarget(newFrame, policy);
        if (accepted) {
            onAnimationRetargeted(handle, it->second);
            return true;
        }

        // Not accepted → either AnimatedValue rejected the retarget
        // (no stored spec) or the new segment was degenerate (newFrom
        // ≈ newTo; AnimatedValue silently marked itself complete).
        // In the degenerate case we reap immediately so the controller's
        // state is internally consistent: hasAnimation(handle) flips to
        // false on return, instead of leaving a zombie entry until the
        // next advanceAnimations() tick. Fires onAnimationComplete
        // once — the same terminal event a naturally-completed
        // animation produces.
        if (it->second.isComplete()) {
            AnimatedValue<QRectF> finished = std::move(it->second);
            m_animations.erase(it);
            onAnimationComplete(handle, finished);
        }
        return false;
    }

    void removeAnimation(Handle handle)
    {
        m_animations.erase(handle);
    }

    void clear()
    {
        m_animations.clear();
    }

    // ─────── State queries ───────

    bool isAnimatingToTarget(Handle handle, const QRectF& target) const
    {
        auto it = m_animations.find(handle);
        if (it == m_animations.end()) {
            return false;
        }
        return it->second.to() == target;
    }

    /// Current interpolated rect for @p handle, or @p fallback when
    /// no animation exists. The fallback is required (no default) so
    /// callers make an explicit choice — passing a default-constructed
    /// QRectF on a miss would silently place the window at (0, 0, 0, 0).
    QRectF currentValue(Handle handle, const QRectF& fallback) const
    {
        auto it = m_animations.find(handle);
        if (it == m_animations.end()) {
            return fallback;
        }
        return it->second.value();
    }

    /// Raw access to the AnimatedValue — adapters use this when they
    /// need to feed the state into compositor-specific paint code
    /// (e.g., KWin's applyTransform reads value() + from() to compute
    /// translate/scale in WindowPaintData). Returns nullptr when no
    /// animation exists for @p handle.
    const AnimatedValue<QRectF>* animationFor(Handle handle) const
    {
        auto it = m_animations.find(handle);
        return (it == m_animations.end()) ? nullptr : &it->second;
    }

    /// Bounding rect covering the full animation path including curve
    /// overshoot, with the adapter's @ref expandedPadding applied.
    /// Returns an empty rect if no animation exists for @p handle.
    QRectF animationBounds(Handle handle) const
    {
        auto it = m_animations.find(handle);
        if (it == m_animations.end()) {
            return QRectF();
        }
        const QMarginsF padding = expandedPadding(handle, it->second);
        return it->second.bounds().marginsAdded(padding);
    }

    // ─────── Per-frame ───────

    /**
     * @brief Advance every in-flight animation by one paint tick.
     *
     * Pulls `dt` from the injected clock's `now()`. Each AnimatedValue
     * reads the same clock reading within a single call, so all
     * animations step in phase with the paint cycle.
     *
     * Re-entrancy: same contract as Phase 2. The completion hook may
     * call back into the controller; entries are erased before the
     * completion hook fires; handles are snapshotted so mutation does
     * not invalidate the outer loop.
     *
     * No-op when no clock has been set.
     */
    void advanceAnimations()
    {
        if (!m_clock) {
            return;
        }

        // Inline capacity covers layout-switch / workspace-switch bulk
        // starts without falling through to heap allocation. 16 (the
        // original Phase 2 figure) was tight for multi-window snap
        // bursts; kMaxInlineHandlesPerTick = 64 seats a full workspace
        // on typical compositor/shell configurations.
        QVarLengthArray<Handle, kMaxInlineHandlesPerTick> handles;
        handles.reserve(m_animations.size());
        for (const auto& [handle, _] : m_animations) {
            handles.append(handle);
        }

        for (Handle handle : handles) {
            auto it = m_animations.find(handle);
            if (it == m_animations.end()) {
                continue; // removed by a prior hook this tick
            }
            if (!isHandleValid(handle)) {
                m_animations.erase(it);
                continue;
            }

            it->second.advance();

            // Re-lookup `it`: AnimatedValue::advance fires
            // onValueChanged / onComplete from MotionSpec, and a
            // user-supplied callback can re-enter the controller and
            // mutate m_animations. If the mutation triggers an
            // unordered_map rehash (insert on a new bucket crossing
            // the load-factor threshold), every outstanding iterator
            // including `it` is invalidated. A fresh find() is O(1)
            // average and cheap — pay it rather than rely on callers
            // to honour the no-reentry contract.
            it = m_animations.find(handle);
            if (it == m_animations.end()) {
                continue; // hook removed this handle
            }

            if (it->second.isComplete()) {
                const QMarginsF padding = expandedPadding(handle, it->second);
                const QRectF bounds = it->second.bounds().marginsAdded(padding);
                AnimatedValue<QRectF> finished = std::move(it->second);
                // Erase BEFORE firing the completion hook so a
                // re-entrant startAnimation(handle, …) from inside the
                // hook registers cleanly and is not clobbered by
                // deferred cleanup. Same contract as Phase 2.
                m_animations.erase(it);
                onAnimationComplete(handle, finished);
                if (bounds.isValid()) {
                    onRepaintNeeded(handle, bounds);
                }
            }
        }
    }

    /**
     * @brief Schedule per-frame repaints covering every in-flight
     *        animation's bounds. Call from postPaintScreen().
     */
    void scheduleRepaints() const
    {
        for (const auto& [handle, anim] : m_animations) {
            const QMarginsF padding = expandedPadding(handle, anim);
            const QRectF bounds = anim.bounds().marginsAdded(padding);
            if (bounds.isValid()) {
                onRepaintNeeded(handle, bounds);
            }
        }
    }

protected:
    // ─────── Subclass hooks ───────

    virtual void onAnimationStarted(Handle /*handle*/, const AnimatedValue<QRectF>& /*anim*/)
    {
    }

    /**
     * @brief Fired when @ref startAnimation displaces an in-flight
     *        animation on the same handle.
     *
     * Semantically distinct from @ref onAnimationComplete — the
     * displaced animation did not reach its target. Consumers that
     * count lifecycle events observe exactly one terminating event per
     * started animation: complete, replaced, or explicit
     * @ref removeAnimation. Default no-op.
     */
    virtual void onAnimationReplaced(Handle /*handle*/, const AnimatedValue<QRectF>& /*displaced*/)
    {
    }

    /**
     * @brief Fired when @ref retarget redirects an in-flight animation.
     *
     * Distinct from @ref onAnimationStarted so adapters can tell a
     * fresh-start boundary (no prior visual state) from a mid-flight
     * redirect (visual continuity preserved). Default no-op — override
     * to splice in per-segment bookkeeping without double-counting
     * starts.
     */
    virtual void onAnimationRetargeted(Handle /*handle*/, const AnimatedValue<QRectF>& /*anim*/)
    {
    }

    virtual void onAnimationComplete(Handle /*handle*/, const AnimatedValue<QRectF>& /*anim*/)
    {
    }

    virtual void onRepaintNeeded(Handle /*handle*/, const QRectF& /*bounds*/) const
    {
    }

    virtual bool isHandleValid(Handle /*handle*/) const
    {
        return true;
    }

    virtual QMarginsF expandedPadding(Handle /*handle*/, const AnimatedValue<QRectF>& /*anim*/) const
    {
        return QMarginsF();
    }

private:
    // Per-tick snapshot of handles to step. Sized to fit a typical
    // full-workspace burst (layout-switch / workspace-switch) in the
    // inline buffer without heap fallback.
    static constexpr int kMaxInlineHandlesPerTick = 64;

    // std::unordered_map (not QHash) because AnimatedValue<T> is
    // move-only — Qt6's QHash still requires copy-constructible values
    // for its internal Node type. The functional difference is
    // negligible for the <100-animation working set; semantically
    // nothing else changes.
    std::unordered_map<Handle, AnimatedValue<QRectF>> m_animations;
    IMotionClock* m_clock = nullptr;
    Profile m_profile;
    int m_minDistance = 0;
    bool m_enabled = true;
};

} // namespace PhosphorAnimation
