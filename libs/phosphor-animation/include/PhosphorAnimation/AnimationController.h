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
 * @brief Outcome of `AnimationController::startAnimation`.
 *
 * Bool overloads collapse every non-`Accepted` outcome into `false`;
 * callers that need to distinguish configuration errors from a
 * legitimate skip (sub-threshold motion, degenerate target) should
 * use `startAnimationWithResult` and switch on this enum.
 */
enum class StartResult {
    Accepted, ///< Animation stored, `onAnimationStarted` fired.
    Disabled, ///< Controller `isEnabled()` returned false.
    NoClock, ///< `clockForHandle(handle)` resolved to null.
    HandleInvalid, ///< `isHandleValid(handle)` returned false.
    PolicyRejected, ///< `SnapPolicy` rejected (below threshold or degenerate).
    /// Defensive fallback. `AnimatedValue::start()` rejected because
    /// `from ≈ to` by `Interpolate<T>::distance`. Unreachable via the
    /// standard path — `SnapPolicy::createSnapSpec` already filters the
    /// degenerate cases before `AnimatedValue::start` sees them. Exists
    /// only so the enum fully enumerates every possible failure from
    /// the underlying primitive, mirroring
    /// `RetargetResult::InternalError`.
    NoMotion,
};

/**
 * @brief Outcome of `AnimationController::retarget`.
 *
 * As with `StartResult`, bool overloads collapse non-`Accepted`
 * outcomes into `false`. `DegenerateReap` is a terminating event —
 * the handle is erased and `onAnimationComplete` fires during the
 * `retarget` call — so a caller seeing `false` cannot assume an
 * animation still exists. Switch on this enum when the distinction
 * matters (e.g. event-count invariants in telemetry).
 */
enum class RetargetResult {
    Accepted, ///< New segment installed, `onAnimationRetargeted` fired.
    UnknownHandle, ///< No animation exists for this handle.
    Disabled, ///< Controller `isEnabled()` returned false.
    HandleInvalid, ///< `isHandleValid(handle)` returned false.
    DegenerateReap, ///< New segment degenerate; entry reaped, `onAnimationComplete` fired.
    InternalError, ///< AnimatedValue rejected retarget without marking complete — a class
                   ///< invariant violation (should not be reachable via the controller
                   ///< path, which always installs a spec on `startAnimation`). The
                   ///< animation is left in place untouched; callers may choose to
                   ///< `removeAnimation` and `startAnimation` again from a known state.
};

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
 * - @ref onAnimationReplaced  — in-flight animation displaced by a new start.
 * - @ref onAnimationRetargeted — in-flight animation redirected (non-terminal).
 * - @ref onAnimationReaped    — batch-reaped via @ref reapAnimationsForClock.
 * - @ref onRepaintNeeded      — fired with a bounds rect for damage.
 * - @ref isHandleValid        — reject stale handles.
 * - @ref expandedPadding      — compositor-specific shadow / decoration margin.
 *
 * ## Lifecycle events
 *
 * Every accepted `startAnimation` produces exactly one terminating
 * event on the controller's hooks:
 *   - @ref onAnimationComplete  — natural termination.
 *   - @ref onAnimationReplaced  — displaced by a second
 *     `startAnimation` on the same handle before the first completed.
 *   - @ref onAnimationReaped    — dropped by
 *     @ref reapAnimationsForClock (output teardown).
 *   - silent removal via @ref removeAnimation / @ref clear — the
 *     caller already has the context; no hook fires.
 * `onAnimationRetargeted` is NOT a terminating event — the same
 * handle keeps ticking, just toward a new target.
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

    /// Profile bundling curve + duration + minDistance (and any future
    /// orchestration fields). Applied to new animations only; in-flight
    /// animations keep their own MotionSpec copy and are unaffected by
    /// subsequent profile swaps — matches Phase 3 decision K
    /// (config-reload immutability).
    ///
    /// `profile.duration` and `profile.minDistance` are clamped to
    /// `[0, kMaxDurationMs]` and `[0, kMaxMinDistancePx]` respectively
    /// in the stored copy — same guards as `setDuration()` /
    /// `setMinDistance()`, applied centrally so callers going through
    /// the profile path cannot bypass them. A settings UI writing a
    /// pathological value (e.g., 60 000 ms duration) through
    /// `setProfile` still lands safely.
    ///
    /// `profile.minDistance` is the single source of truth for the
    /// snap-skip threshold — both `setMinDistance()` and `setProfile()`
    /// write through to the same field; `SnapPolicy::createSnapSpec`
    /// reads `profile.effectiveMinDistance()` at animation-start time.
    void setProfile(const Profile& profile)
    {
        m_profile = profile;
        if (m_profile.duration) {
            m_profile.duration = qBound(qreal(0.0), *m_profile.duration, kMaxDurationMs);
        }
        if (m_profile.minDistance) {
            m_profile.minDistance = qBound(0, *m_profile.minDistance, kMaxMinDistancePx);
        }
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

    /// Upper bound on duration clamp in `setDuration`. 10 s is already
    /// far outside any reasonable snap-animation range — the clamp
    /// exists to keep a mis-configured settings UI from stalling the
    /// controller with, e.g., 60 s values.
    static constexpr qreal kMaxDurationMs = 10000.0;

    /// Upper bound on minimum-distance clamp in `setMinDistance`. Same
    /// rationale as `kMaxDurationMs` — protects against pathological
    /// settings inputs, not a physical limit.
    static constexpr int kMaxMinDistancePx = 10000;

    /// Convenience setter — equivalent to modifying `profile().duration`.
    /// Clamps to `[0, kMaxDurationMs]` ms (same range as Phase 2).
    void setDuration(qreal ms)
    {
        m_profile.duration = qBound(qreal(0.0), ms, kMaxDurationMs);
    }
    qreal duration() const
    {
        return m_profile.effectiveDuration();
    }

    /// Minimum translate distance (in px) before a transition is worth
    /// animating. Clamped to `[0, kMaxMinDistancePx]`. Callers wanting a
    /// tighter clamp apply it themselves.
    ///
    /// Convenience setter — equivalent to modifying
    /// `profile().minDistance`. Both paths mutate the same underlying
    /// field on `m_profile`, so a settings UI can use either entry
    /// point interchangeably.
    void setMinDistance(int pixels)
    {
        m_profile.minDistance = qBound(0, pixels, kMaxMinDistancePx);
    }
    int minDistance() const
    {
        return m_profile.effectiveMinDistance();
    }

    /// Default retarget policy stamped into every new animation's
    /// `MotionSpec::retargetPolicy`. The two-argument `retarget(handle,
    /// newFrame)` overloads read the stamped value back out per
    /// animation, so a settings UI that wants drag-through-zones to
    /// feel like a reset-on-retarget (rather than velocity-preserved
    /// redirection) sets this once and every subsequent start +
    /// policy-less retarget honour it.
    ///
    /// In-flight animations keep their own captured MotionSpec copy
    /// and are unaffected by a subsequent policy swap — matches the
    /// config-reload immutability contract on `setProfile`.
    ///
    /// `PreserveVelocity` (the default) carries the world-space rate
    /// across the retarget boundary on stateful curves and degrades
    /// to position-continuous on stateless curves.
    void setRetargetPolicy(RetargetPolicy policy)
    {
        m_retargetPolicy = policy;
    }
    RetargetPolicy retargetPolicy() const
    {
        return m_retargetPolicy;
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
     * Returns `false` when any of the rejection paths enumerated in
     * @ref StartResult trigger. Use @ref startAnimationWithResult if
     * the caller needs to distinguish configuration errors from a
     * legitimate policy skip.
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
        return startAnimationWithResult(handle, oldFrame, newFrame) == StartResult::Accepted;
    }

    /**
     * @brief Enum-returning companion to @ref startAnimation.
     *
     * Same semantics; distinguishes configuration errors (Disabled /
     * NoClock / HandleInvalid) from legitimate policy skips
     * (PolicyRejected / NoMotion). Callers that need to surface
     * diagnostics to users or event counters use this overload.
     */
    StartResult startAnimationWithResult(Handle handle, const QRectF& oldFrame, const QRectF& newFrame)
    {
        if (!m_enabled) {
            return StartResult::Disabled;
        }
        if (!isHandleValid(handle)) {
            return StartResult::HandleInvalid;
        }

        IMotionClock* clock = clockForHandle(handle);
        if (!clock) {
            return StartResult::NoClock;
        }

        SnapPolicy::SnapParams params;
        params.profile = m_profile;
        params.retargetPolicy = m_retargetPolicy;
        // minDistance rides on the profile — SnapPolicy reads
        // params.profile.effectiveMinDistance(). See setMinDistance /
        // setProfile — both funnel into m_profile.minDistance.
        const auto spec = SnapPolicy::createSnapSpec(oldFrame, newFrame, params, clock);
        if (!spec) {
            return StartResult::PolicyRejected;
        }

        AnimatedValue<QRectF> anim;
        if (!anim.start(oldFrame, newFrame, *spec)) {
            return StartResult::NoMotion;
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
        return StartResult::Accepted;
    }

    /**
     * @brief Redirect an in-flight animation to a new target.
     *
     * Preserves the visible value (new segment starts from current
     * value) and reshapes state per @p policy. Returns `false` when
     * any of the rejection paths enumerated in @ref RetargetResult
     * trigger — use @ref retargetWithResult if the caller needs to
     * distinguish them.
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
     *
     * Gated by `isEnabled()` and `isHandleValid(handle)` for symmetry
     * with `startAnimation`; disabling the controller mid-animation
     * blocks further redirects until re-enabled (the existing
     * animation still progresses to completion on the clock's own
     * ticks — use `removeAnimation(handle)` to force termination).
     */
    bool retarget(Handle handle, const QRectF& newFrame, RetargetPolicy policy)
    {
        return retargetWithResult(handle, newFrame, policy) == RetargetResult::Accepted;
    }

    /**
     * @brief Redirect using the per-animation stored retarget policy.
     *
     * Reads `animationFor(handle)->spec().retargetPolicy` (stamped by
     * `startAnimation` from the controller's `retargetPolicy()`) and
     * forwards to the three-argument overload. Callers that want
     * drag-through-zones to feel like a reset-on-retarget set
     * `setRetargetPolicy(ResetVelocity)` once at configure-time rather
     * than passing the enum at every call site.
     *
     * Falls through to the controller's current default
     * `retargetPolicy()` when the handle is unknown — the call still
     * fails with `RetargetResult::UnknownHandle`, but the forwarded
     * policy avoids a second branch in the dispatcher.
     */
    bool retarget(Handle handle, const QRectF& newFrame)
    {
        return retargetWithResult(handle, newFrame) == RetargetResult::Accepted;
    }

    /**
     * @brief Enum-returning companion to @ref retarget.
     *
     * Returns `RetargetResult::DegenerateReap` when the redirect
     * collapsed the animation — in that case `hasAnimation(handle)`
     * is false on return and `onAnimationComplete` has already fired,
     * so the caller must NOT treat this as a "start over" signal
     * (that's a double-completion).
     */
    RetargetResult retargetWithResult(Handle handle, const QRectF& newFrame, RetargetPolicy policy)
    {
        if (!m_enabled) {
            return RetargetResult::Disabled;
        }
        if (!isHandleValid(handle)) {
            return RetargetResult::HandleInvalid;
        }
        auto it = m_animations.find(handle);
        if (it == m_animations.end()) {
            return RetargetResult::UnknownHandle;
        }
        const bool accepted = it->second.retarget(newFrame, policy);
        if (accepted) {
            onAnimationRetargeted(handle, it->second);
            return RetargetResult::Accepted;
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
            return RetargetResult::DegenerateReap;
        }
        // Pathological: AnimatedValue rejected without marking complete
        // (no stored spec — should not happen via controller path since
        // startAnimation always installs a spec). Distinct from
        // UnknownHandle so callers can tell "I passed a handle that
        // never existed" from "the controller's internal state is
        // inconsistent". The animation entry is left in place untouched;
        // a subsequent removeAnimation + startAnimation from the caller
        // restores a known good state.
        return RetargetResult::InternalError;
    }

    /**
     * @brief Policy-less overload that reads the per-animation stored
     *        `MotionSpec::retargetPolicy`.
     *
     * `startAnimation` stamps `m_retargetPolicy` into the spec, so the
     * default path is "controller-configured default". Callers that
     * need a per-call policy override pass it to the three-argument
     * overload directly. Symmetry with `AnimatedValue<T>::retarget(newTo)`
     * which uses `m_spec.retargetPolicy` for the same reason.
     */
    RetargetResult retargetWithResult(Handle handle, const QRectF& newFrame)
    {
        auto it = m_animations.find(handle);
        const RetargetPolicy policy = (it != m_animations.end()) ? it->second.spec().retargetPolicy : m_retargetPolicy;
        return retargetWithResult(handle, newFrame, policy);
    }

    void removeAnimation(Handle handle)
    {
        m_animations.erase(handle);
    }

    void clear()
    {
        m_animations.clear();
    }

    /**
     * @brief Reap every animation whose MotionSpec captured @p clock.
     *
     * Intended for compositor-output teardown: when an output
     * disconnects, its `IMotionClock` is about to be destroyed — every
     * animation routing `dt` through that clock must be reaped first or
     * the next `advanceAnimations()` tick reads through a dangling
     * pointer.
     *
     * Fires @ref onAnimationReaped for each removed entry so consumers
     * that maintain per-animation accounting (telemetry counters,
     * damage-region accumulators, UX "window X snapped to zone Y"
     * tooltips) can balance their started-vs-terminated event counts
     * without special-casing output teardown. The hook is distinct from
     * `onAnimationComplete` / `onAnimationReplaced` so adapters can
     * opt-in to batch cancellation notifications without treating them
     * as natural completions.
     *
     * The reference passed to `onAnimationReaped` points to a
     * locally-moved copy on the controller's stack — valid for the
     * duration of the hook only. See the subclass-hooks reference
     * lifetime contract.
     *
     * Safe to call with @p clock == nullptr (no-op — nothing matches
     * the fallback-sentinel shape). Returns the number of reaped
     * animations so callers can log or assert.
     */
    int reapAnimationsForClock(const IMotionClock* clock)
    {
        if (!clock) {
            return 0;
        }
        int reaped = 0;
        for (auto it = m_animations.begin(); it != m_animations.end();) {
            if (it->second.spec().clock == clock) {
                AnimatedValue<QRectF> reapedAnim = std::move(it->second);
                const Handle handle = it->first;
                it = m_animations.erase(it);
                onAnimationReaped(handle, reapedAnim);
                ++reaped;
            } else {
                ++it;
            }
        }
        return reaped;
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
    ///
    /// ## Pointer lifetime
    ///
    /// `std::unordered_map` guarantees that element references
    /// survive insertions and rehashes, so the returned pointer stays
    /// valid across subsequent `startAnimation`/`retarget` calls on
    /// *other* handles. It does NOT survive `removeAnimation(handle)`,
    /// `clear()`, or the per-tick reap paths inside `advanceAnimations`
    /// (completion, handle-invalid reap). Safe pattern: consume the
    /// pointer fully within the same synchronous scope that obtained
    /// it; do not cache across controller mutations on @p handle.
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
        // No top-level clock guard: per-handle routing via
        // `clockForHandle` allows a null `m_clock` default so long as
        // each started animation captured a non-null clock into its
        // own spec (validated at `startAnimation` time). Individual
        // `AnimatedValue::advance()` calls short-circuit when their
        // spec's clock is null, so iterating an empty map or a map of
        // correctly-specced animations is both safe and cheap.

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

            // Per-tick clock re-resolution. If `clockForHandle(handle)`
            // now resolves to a different pointer than the one captured
            // at start (window migrated between outputs, QML item re-
            // parented to a different QQuickWindow), rebind. Relies on
            // the epoch contract documented on
            // AnimatedValue::rebindClock — both shipped clocks share
            // std::chrono::steady_clock so the rebind is a pointer
            // swap with no timestamp rebase. Null result leaves the
            // animation on its existing clock (no regression: if the
            // resolver can't produce a clock now, the captured one is
            // still the best we have).
            if (IMotionClock* resolved = clockForHandle(handle)) {
                if (resolved != it->second.spec().clock) {
                    it->second.rebindClock(resolved);
                }
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
    //
    // ## Reference lifetime contract for `const AnimatedValue<QRectF>&`
    //
    // The reference parameter is valid for the duration of the hook
    // call only. For `onAnimationStarted` / `onAnimationRetargeted` the
    // reference points into `m_animations` (survives until the next
    // mutation on the same handle). For `onAnimationReplaced` /
    // `onAnimationComplete` the reference points to a locally-moved
    // copy on the controller's stack — it goes out of scope as soon
    // as the hook returns. Hooks MUST NOT store the reference for
    // later use; if downstream code needs post-hook access, copy the
    // interesting fields out by value (`anim.to()`, `anim.value()`,
    // etc.) before returning from the hook.

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
     *
     * The @p displaced reference is scoped to the call — see the
     * lifetime contract above.
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

    /**
     * @brief Fired exactly once when an animation completes naturally.
     *
     * The @p anim reference is scoped to the call (points to a locally
     * moved-from map entry) — see the lifetime contract above.
     */
    virtual void onAnimationComplete(Handle /*handle*/, const AnimatedValue<QRectF>& /*anim*/)
    {
    }

    /**
     * @brief Fired when @ref reapAnimationsForClock drops an animation
     *        as part of output/clock teardown.
     *
     * Distinct from @ref onAnimationComplete (the animation did not
     * reach its target) and @ref onAnimationReplaced (no new segment
     * is about to start in its place). Consumers that count lifecycle
     * events override this to balance started-vs-terminated accounting
     * under multi-output hotplug; adapters that only care about
     * user-visible completion can leave the default no-op.
     *
     * The @p anim reference is scoped to the call — see the lifetime
     * contract above.
     */
    virtual void onAnimationReaped(Handle /*handle*/, const AnimatedValue<QRectF>& /*reaped*/)
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

    /**
     * @brief Resolve the clock to drive animations for @p handle.
     *
     * Default implementation returns the controller-wide `m_clock`
     * (the one set via `setClock()`). Override when per-handle routing
     * is required — e.g., a compositor adapter that binds animations
     * to the clock matching each window's current output so mixed
     * refresh-rate displays phase-lock independently.
     *
     * ## Call sites
     *
     * - `startAnimation`: captures the resolved clock into the
     *   animation's `MotionSpec`.
     * - `advanceAnimations`: re-resolves once per tick and rebinds
     *   (via `AnimatedValue::rebindClock`) if the pointer changed —
     *   so a handle that migrates between outputs mid-animation
     *   automatically follows. Rebind relies on the epoch contract
     *   (both clocks must be `std::chrono::steady_clock`-backed);
     *   the shipped clocks both meet this, so per-tick rebind is
     *   safe for all in-tree consumers.
     *
     * Override implementations should be cheap (O(1) lookup) since
     * they are called on every `advanceAnimations` tick per handle.
     */
    virtual IMotionClock* clockForHandle(Handle /*handle*/) const
    {
        return m_clock;
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
    // minDistance lives on m_profile.minDistance — no separate field.
    // setMinDistance/setProfile both write through; minDistance() reads
    // m_profile.effectiveMinDistance(). Single source of truth, round-
    // trips through Profile serialisation, and settings UIs can use
    // either entry point interchangeably.
    Profile m_profile;
    // Controller-configured default stamped into every new animation's
    // MotionSpec at startAnimation time. The two-argument retarget()
    // overloads read the stamped per-animation value back out so a
    // policy change only affects subsequent starts, not in-flight
    // animations (config-reload immutability — same contract as
    // setProfile).
    RetargetPolicy m_retargetPolicy = RetargetPolicy::PreserveVelocity;
    bool m_enabled = true;
};

} // namespace PhosphorAnimation
