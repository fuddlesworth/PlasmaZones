// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimationMath.h>
#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/WindowMotion.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QHash>
#include <QMarginsF>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QVarLengthArray>

#include <chrono>
#include <memory>
#include <utility>

namespace PhosphorAnimation {

/**
 * @brief Compositor-agnostic animation state machine.
 *
 * Owns a `QHash<Handle, WindowMotion>` plus the configuration knobs
 * (curve, duration, minDistance, enabled) and the per-frame progression
 * loop. Subclasses bind the @c Handle template parameter to their
 * compositor's window-handle type (e.g. `KWin::EffectWindow*` for the
 * KDE adapter, `wlr_surface*` for a future Wayfire adapter, an integer
 * id for QML overlays) and override the four virtual hooks to splice
 * the controller into the surrounding paint pipeline.
 *
 * ## Why a template
 *
 * The handle type is the only compositor-specific dimension of the
 * state machine — the lifecycle, progression math, and bounds
 * computation are all handle-agnostic. A template keeps the controller
 * pure C++ with no QObject overhead and lets each adapter use its own
 * native pointer / id type without indirection through `void*`.
 *
 * ## Virtual hooks
 *
 * - @ref onAnimationStarted  — fired once when @ref startAnimation
 *   accepts a transition. Adapters typically request a full repaint.
 * - @ref onAnimationComplete — fired once when an animation completes.
 *   Adapters typically request a final full repaint to clean up the
 *   residual transform from the last frame.
 * - @ref onRepaintNeeded     — fired with a bounding rect when the
 *   compositor should invalidate that region (per-frame progression
 *   schedule, completion damage, etc.).
 * - @ref isHandleValid       — let the compositor reject stale handles
 *   (e.g., windows that have entered the "deleted" state). The
 *   controller calls this before every per-frame update; invalid
 *   handles are removed without firing any other hook.
 * - @ref expandedPadding     — give the controller compositor-specific
 *   padding (window shadows / decorations) to widen the repaint rect.
 *
 * ## Thread safety
 *
 * Not internally synchronized — this is a GUI-thread / compositor-
 * thread state machine. Concurrency is the consumer's problem.
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

    void setEnabled(bool enabled)
    {
        m_enabled = enabled;
    }
    bool isEnabled() const
    {
        return m_enabled;
    }

    void setDuration(qreal ms)
    {
        m_duration = ms;
    }
    qreal duration() const
    {
        return m_duration;
    }

    /// Set the curve used for new animations. May be null — null means
    /// linear progression (cachedProgress = normalized t).
    void setCurve(std::shared_ptr<const Curve> curve)
    {
        m_curve = std::move(curve);
    }
    std::shared_ptr<const Curve> curve() const
    {
        return m_curve;
    }

    /// Minimum translate distance (in px) before a transition is worth
    /// animating. Clamped to [0, 10000] — the upper bound is a sanity
    /// cap so a pathological config value doesn't silently disable every
    /// animation. Callers that want a tighter clamp should apply it
    /// themselves (the effect's settings loader clamps to [0, 200]).
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
        return !m_motions.isEmpty();
    }

    bool hasAnimation(Handle handle) const
    {
        return m_motions.contains(handle);
    }

    /**
     * @brief Build a @ref WindowMotion for @p handle and register it.
     *
     * Returns false (and does nothing) when:
     *   - @ref isEnabled() is false,
     *   - @ref isHandleValid returns false for @p handle (adapters
     *     reject stale / null handles here so nothing downstream needs
     *     to re-check),
     *   - the geometry-based skip rules in @ref AnimationMath::createSnapMotion
     *     reject the transition (degenerate target, sub-threshold delta
     *     with no scale change).
     *
     * Re-calling for a handle that already has an in-flight motion
     * overwrites it; @ref onAnimationStarted fires again. Adapters that
     * want smooth retarget should pass the current @ref currentVisualPosition
     * / @ref currentVisualSize as the start state before overwriting.
     *
     * Otherwise the motion is stored, @ref onAnimationStarted is fired,
     * and the function returns true.
     */
    bool startAnimation(Handle handle, const QPointF& oldPosition, const QSizeF& oldSize, const QRect& targetGeometry)
    {
        if (!m_enabled) {
            return false;
        }
        if (!isHandleValid(handle)) {
            return false;
        }

        auto motion =
            AnimationMath::createSnapMotion(oldPosition, oldSize, targetGeometry, m_duration, m_curve, m_minDistance);
        if (!motion) {
            return false;
        }

        m_motions[handle] = *motion;
        onAnimationStarted(handle, m_motions[handle]);
        return true;
    }

    void removeAnimation(Handle handle)
    {
        m_motions.remove(handle);
    }

    void clear()
    {
        m_motions.clear();
    }

    // ─────── State queries ───────

    bool isAnimatingToTarget(Handle handle, const QRect& targetGeometry) const
    {
        auto it = m_motions.constFind(handle);
        if (it == m_motions.constEnd()) {
            return false;
        }
        return it->targetGeometry == targetGeometry;
    }

    /// Current visual top-left position (lerped between start and target).
    /// Returns @p fallback when no motion exists for @p handle.
    ///
    /// The fallback is mandatory (no default) so callers make an explicit
    /// choice — passing `{}` silently on a miss would place the window at
    /// origin. Adapters should typically pass the window's current frame
    /// geometry top-left so a stray call outside `hasAnimation()` still
    /// yields a sensible value.
    QPointF currentVisualPosition(Handle handle, const QPointF& fallback) const
    {
        auto it = m_motions.constFind(handle);
        if (it == m_motions.constEnd()) {
            return fallback;
        }
        return it->currentVisualPosition();
    }

    /// Current visual size (lerped between start and target).
    /// Returns @p fallback when no motion exists for @p handle.
    /// See @ref currentVisualPosition for the fallback rationale.
    QSizeF currentVisualSize(Handle handle, const QSizeF& fallback) const
    {
        auto it = m_motions.constFind(handle);
        if (it == m_motions.constEnd()) {
            return fallback;
        }
        return it->currentVisualSize();
    }

    /// Read-only access to the underlying motion record. Returns
    /// nullptr if @p handle has no active animation. Adapters use this
    /// when they need to feed the motion into compositor-specific paint
    /// code (e.g., applying transforms in KWin's `WindowPaintData`).
    const WindowMotion* motionFor(Handle handle) const
    {
        auto it = m_motions.constFind(handle);
        return (it == m_motions.constEnd()) ? nullptr : &it.value();
    }

    /// Bounding rect covering the full animation path including
    /// curve overshoot, with the adapter's @ref expandedPadding applied.
    QRectF animationBounds(Handle handle) const
    {
        auto it = m_motions.constFind(handle);
        if (it == m_motions.constEnd()) {
            return QRectF();
        }
        const QMarginsF padding = expandedPadding(handle, *it);
        return AnimationMath::repaintBounds(it->startPosition, it->startSize, it->targetGeometry, it->curve, padding);
    }

    // ─────── Per-frame ───────

    /**
     * @brief Update cached progress for every active animation, fire
     * completion / repaint hooks, and prune.
     *
     * Call exactly once per paint cycle with the compositor's
     * vsync-aligned @p presentTime. Order of operations per handle:
     *   1. Prune (without firing hooks) if @ref isHandleValid returns false.
     *   2. Otherwise latch start time + cache eased progress.
     *   3. If complete, compute final-frame bounds, remove the entry
     *      from the active map, fire @ref onAnimationComplete, then
     *      fire @ref onRepaintNeeded.
     *
     * ## Re-entrancy contract
     *
     * The completion hook may call back into the controller —
     * @ref startAnimation, @ref removeAnimation, or @ref clear are all
     * safe. The entry for the completing handle is erased *before* the
     * hook fires, so a re-registration via @ref startAnimation inserts
     * cleanly and survives the remainder of this call. Handles are
     * snapshotted before iteration, so hook-driven mutation of
     * @c m_motions cannot invalidate the outer loop's iterator.
     */
    void advanceAnimations(std::chrono::milliseconds presentTime)
    {
        // Snapshot handles before iteration — the completion hook may
        // mutate m_motions (retarget / chained animations / clear), and a
        // live QHash iterator would be invalidated by an insert. Looking
        // up each handle fresh inside the loop is O(1) and robust.
        QVarLengthArray<Handle, 16> handles;
        handles.reserve(m_motions.size());
        for (auto it = m_motions.constBegin(); it != m_motions.constEnd(); ++it) {
            handles.append(it.key());
        }

        for (Handle handle : handles) {
            auto it = m_motions.find(handle);
            if (it == m_motions.end()) {
                continue; // Removed by a prior hook on this same tick.
            }

            if (!isHandleValid(handle)) {
                m_motions.erase(it);
                continue;
            }

            it->updateProgress(presentTime);

            if (it->isComplete(presentTime)) {
                const QMarginsF padding = expandedPadding(handle, *it);
                const QRectF bounds = AnimationMath::repaintBounds(it->startPosition, it->startSize, it->targetGeometry,
                                                                   it->curve, padding);
                const WindowMotion finished = *it;
                // Erase BEFORE firing the completion hook so a re-entrant
                // startAnimation(handle, …) from inside the hook
                // registers cleanly (hasAnimation(handle) is false during
                // the hook) and is not clobbered by any deferred cleanup.
                m_motions.erase(it);
                onAnimationComplete(handle, finished);
                if (bounds.isValid()) {
                    onRepaintNeeded(handle, bounds);
                }
            }
        }
    }

    /**
     * @brief Schedule per-frame repaints covering every in-flight
     * animation's bounds. Call from postPaintScreen().
     *
     * Marked `const` because it does not mutate the controller. The
     * `onRepaintNeeded` hook it dispatches through may still produce
     * external side effects (e.g. `KWin::effects->addRepaint`) — that's
     * the adapter's concern, not the controller's.
     */
    void scheduleRepaints() const
    {
        for (auto it = m_motions.constBegin(); it != m_motions.constEnd(); ++it) {
            const QMarginsF padding = expandedPadding(it.key(), *it);
            const QRectF bounds =
                AnimationMath::repaintBounds(it->startPosition, it->startSize, it->targetGeometry, it->curve, padding);
            if (bounds.isValid()) {
                onRepaintNeeded(it.key(), bounds);
            }
        }
    }

protected:
    // ─────── Subclass hooks ───────

    /// Fired once when @ref startAnimation accepts a transition. Default no-op.
    virtual void onAnimationStarted(Handle /*handle*/, const WindowMotion& /*motion*/)
    {
    }

    /// Fired once when an animation completes. The entry has already
    /// been removed from the active map by the time this fires, so
    /// `hasAnimation(handle)` returns false inside the hook — a
    /// re-entrant `startAnimation(handle, …)` is therefore safe and
    /// registers a fresh motion without conflict. Default no-op.
    virtual void onAnimationComplete(Handle /*handle*/, const WindowMotion& /*motion*/)
    {
    }

    /// Fired with a bounding rect that should be invalidated. Adapters
    /// translate this into compositor-specific repaint requests.
    /// Default no-op.
    virtual void onRepaintNeeded(Handle /*handle*/, const QRectF& /*bounds*/) const
    {
    }

    /// Validate a handle before per-frame work. Adapters override to
    /// reject handles that have entered a deleted / detached state.
    /// Default: every handle is valid.
    virtual bool isHandleValid(Handle /*handle*/) const
    {
        return true;
    }

    /// Compositor-specific padding around the frame geometry (window
    /// shadows, decorations, focus-ring bleed). Default: zero margins.
    virtual QMarginsF expandedPadding(Handle /*handle*/, const WindowMotion& /*motion*/) const
    {
        return QMarginsF();
    }

private:
    QHash<Handle, WindowMotion> m_motions;
    bool m_enabled = true;
    qreal m_duration = 150.0;
    std::shared_ptr<const Curve> m_curve;
    int m_minDistance = 0;
};

} // namespace PhosphorAnimation
