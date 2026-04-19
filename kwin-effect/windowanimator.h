// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/AnimationController.h>

#include <functional>

namespace KWin {
class EffectWindow;
class LogicalOutput;
class WindowPaintData;
}

namespace PhosphorAnimation {
class IMotionClock;
}

namespace PlasmaZones {

/**
 * @brief KWin adapter on top of `PhosphorAnimation::AnimationController`.
 *
 * The compositor-agnostic state machine (lifecycle, progression via
 * `AnimatedValue<QRectF>`, clock integration, bounds computation,
 * completion + repaint hooks) lives in the library base. This subclass
 * binds the handle type to `KWin::EffectWindow*`, routes the five
 * virtual hooks into KWin's effect pipeline, and adds
 * `applyTransform` — the only KWin-coupled call still needed.
 *
 * When a window is snapped to a zone, the caller applies moveResize()
 * immediately to set the final geometry. The animator provides visual
 * translation and scale transforms in paintWindow() that morph the
 * window from its old position/size to the new one. This follows the
 * standard KDE effect pattern — effects are purely visual overlays on
 * the compositing pipeline and never call moveResize() per-frame.
 *
 * Timing is driven by per-output `CompositorClock` instances resolved
 * via the `OutputClockResolver` callback — mixed-refresh-rate displays
 * phase-lock independently instead of beating against a shared process-
 * wide clock. The resolver is injected by the effect at construction
 * time; the animator's `clockForHandle` override calls back into it
 * with each window's current `LogicalOutput`.
 */
class WindowAnimator : public PhosphorAnimation::AnimationController<KWin::EffectWindow*>
{
public:
    /// Callback resolving an output to the matching IMotionClock. The
    /// effect owns the clock instances; the animator just routes via
    /// this resolver at `startAnimation` time. May return nullptr for
    /// an unknown output — in that case the controller's default
    /// clock (set via setClock) is used.
    using OutputClockResolver = std::function<PhosphorAnimation::IMotionClock*(KWin::LogicalOutput*)>;

    /// Install the per-output clock resolver. Safe to call at any time;
    /// takes effect for subsequent `startAnimation` calls. In-flight
    /// animations keep the clock captured at their own start time.
    void setOutputClockResolver(OutputClockResolver resolver);

    /// Apply translate offset + scale transform to @p data so the window
    /// renders at the current interpolated visual position/size. No-op
    /// when @p window has no active animation or the motion is still
    /// pending (startTime not latched — one frame between
    /// startAnimation and the first advanceAnimations tick).
    void applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const;

protected:
    void onAnimationStarted(KWin::EffectWindow* window, const PhosphorAnimation::AnimatedValue<QRectF>& anim) override;
    void onAnimationComplete(KWin::EffectWindow* window, const PhosphorAnimation::AnimatedValue<QRectF>& anim) override;
    void onRepaintNeeded(KWin::EffectWindow* window, const QRectF& bounds) const override;
    bool isHandleValid(KWin::EffectWindow* window) const override;
    QMarginsF expandedPadding(KWin::EffectWindow* window,
                              const PhosphorAnimation::AnimatedValue<QRectF>& anim) const override;
    PhosphorAnimation::IMotionClock* clockForHandle(KWin::EffectWindow* window) const override;

private:
    OutputClockResolver m_outputClockResolver;
};

} // namespace PlasmaZones
