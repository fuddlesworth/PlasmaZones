// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/Interpolate.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/RetargetPolicy.h>
#include <PhosphorAnimation/SnapPolicy.h>

#include <QMarginsF>
#include <QRectF>
#include <QVarLengthArray>

#include <cmath>
#include <functional>
#include <memory>
#include <unordered_map>

namespace KWin {
class EffectWindow;
class LogicalOutput;
class WindowPaintData;
}

namespace PhosphorAnimation {
class Curve;
}

// ─────── Enums formerly in AnimationController.h ───────
// Kept in PhosphorAnimation namespace so PlasmaZonesEffect call sites
// (e.g. PhosphorAnimation::RetargetResult::DegenerateReap) compile
// unchanged without modifying plasmazoneseffect.cpp.

namespace PhosphorAnimation {

enum class StartResult {
    Accepted,
    AcceptedThenRemoved,
    Disabled,
    NoClock,
    HandleInvalid,
    PolicyRejected,
    NoMotion,
};

enum class RetargetResult {
    Accepted,
    UnknownHandle,
    Disabled,
    HandleInvalid,
    DegenerateReap,
    InternalError,
};

} // namespace PhosphorAnimation

namespace PlasmaZones {

/**
 * @brief KWin window snap-animation controller.
 *
 * Self-contained controller that owns per-window AnimatedValue<QRectF>
 * state, drives it via per-output IMotionClock instances, and splices
 * the results into KWin's paint pipeline via applyTransform().
 *
 * Formerly split across the library-side AnimationController<Handle>
 * template and this KWin-specific subclass. The template had exactly
 * one instantiation (Handle = KWin::EffectWindow*), so the two layers
 * are now merged here for locality and to eliminate the virtual-dispatch
 * overhead on the per-tick hooks.
 */
class WindowAnimator
{
public:
    WindowAnimator() = default;
    ~WindowAnimator() = default;

    WindowAnimator(const WindowAnimator&) = delete;
    WindowAnimator& operator=(const WindowAnimator&) = delete;

    // ─── Clock resolver callback ───

    using OutputClockResolver = std::function<PhosphorAnimation::IMotionClock*(KWin::LogicalOutput*)>;
    using AnimationCompleteCallback = std::function<void(KWin::EffectWindow*)>;

    void setOutputClockResolver(OutputClockResolver resolver);
    void setOnAnimationCompleteCallback(AnimationCompleteCallback callback);

    // ─── Configuration ───

    void setClock(PhosphorAnimation::IMotionClock* clock);
    PhosphorAnimation::IMotionClock* clock() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void setProfile(const PhosphorAnimation::Profile& profile);
    const PhosphorAnimation::Profile& profile() const;

    void setCurve(std::shared_ptr<const PhosphorAnimation::Curve> curve);
    std::shared_ptr<const PhosphorAnimation::Curve> curve() const;

    static constexpr qreal kMaxDurationMs = 10000.0;
    static constexpr int kMaxMinDistancePx = 10000;

    void setDuration(qreal ms);
    qreal duration() const;

    void setMinDistance(int pixels);
    int minDistance() const;

    void setRetargetPolicy(PhosphorAnimation::RetargetPolicy policy);
    PhosphorAnimation::RetargetPolicy retargetPolicy() const;

    // ─── Lifecycle ───

    bool hasActiveAnimations() const;
    bool hasAnimation(KWin::EffectWindow* handle) const;

    /// Start an animation. When @p profileOverride is non-null, the per-call
    /// curve / duration / minDistance / sequence overrides on it replace the
    /// configured global profile (`m_profile`) for THIS animation only —
    /// subsequent animations on other windows continue to use the global
    /// profile. Used by the per-window animation rule cascade to apply a
    /// per-window-class motion override without mutating shared animator
    /// state.
    bool startAnimation(KWin::EffectWindow* handle, const QRectF& oldFrame, const QRectF& newFrame,
                        const PhosphorAnimation::Profile* profileOverride = nullptr);
    PhosphorAnimation::StartResult
    startAnimationWithResult(KWin::EffectWindow* handle, const QRectF& oldFrame, const QRectF& newFrame,
                             const PhosphorAnimation::Profile* profileOverride = nullptr);

    bool retarget(KWin::EffectWindow* handle, const QRectF& newFrame, PhosphorAnimation::RetargetPolicy policy);
    bool retarget(KWin::EffectWindow* handle, const QRectF& newFrame);

    PhosphorAnimation::RetargetResult retargetWithResult(KWin::EffectWindow* handle, const QRectF& newFrame,
                                                         PhosphorAnimation::RetargetPolicy policy);
    PhosphorAnimation::RetargetResult retargetWithResult(KWin::EffectWindow* handle, const QRectF& newFrame);

    void removeAnimation(KWin::EffectWindow* handle);
    void clear();
    int reapAnimationsForClock(const PhosphorAnimation::IMotionClock* clock);

    // ─── State queries ───

    bool isAnimatingToTarget(KWin::EffectWindow* handle, const QRectF& target) const;
    QRectF currentValue(KWin::EffectWindow* handle, const QRectF& fallback) const;
    const PhosphorAnimation::AnimatedValue<QRectF>* animationFor(KWin::EffectWindow* handle) const;
    QRectF animationBounds(KWin::EffectWindow* handle) const;

    // ─── Per-frame ───

    void advanceAnimations();
    void scheduleRepaints() const;

    // ─── KWin-specific ───

    void applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const;

private:
    // ─── Hook methods (formerly virtual overrides) ───

    void onAnimationStarted(KWin::EffectWindow* window, const PhosphorAnimation::AnimatedValue<QRectF>& anim);
    void onAnimationComplete(KWin::EffectWindow* window, const PhosphorAnimation::AnimatedValue<QRectF>& anim);
    void onAnimationReplaced(KWin::EffectWindow* window, const PhosphorAnimation::AnimatedValue<QRectF>& displaced);
    void onAnimationRetargeted(KWin::EffectWindow* window, const PhosphorAnimation::AnimatedValue<QRectF>& anim);
    void onAnimationReaped(KWin::EffectWindow* window, const PhosphorAnimation::AnimatedValue<QRectF>& anim);
    void onAnimationAbandoned(KWin::EffectWindow* window, const PhosphorAnimation::AnimatedValue<QRectF>& anim);
    void onRepaintNeeded(KWin::EffectWindow* window, const QRectF& bounds) const;
    bool isHandleValid(KWin::EffectWindow* window) const;
    QMarginsF expandedPadding(KWin::EffectWindow* window, const PhosphorAnimation::AnimatedValue<QRectF>& anim) const;
    PhosphorAnimation::IMotionClock* clockForHandle(KWin::EffectWindow* window) const;

    // ─── Helpers ───

    static void clampProfile(PhosphorAnimation::Profile& profile);

    // ─── Data ───

    static constexpr int kMaxInlineHandlesPerTick = 64;

    std::unordered_map<KWin::EffectWindow*, PhosphorAnimation::AnimatedValue<QRectF>> m_animations;
    PhosphorAnimation::IMotionClock* m_clock = nullptr;
    PhosphorAnimation::Profile m_profile;
    PhosphorAnimation::RetargetPolicy m_retargetPolicy = PhosphorAnimation::RetargetPolicy::PreserveVelocity;
    bool m_enabled = true;

    OutputClockResolver m_outputClockResolver;
    AnimationCompleteCallback m_onAnimationCompleteCallback;
};

} // namespace PlasmaZones
