// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/AnimationController.h>

namespace KWin {
class EffectWindow;
class WindowPaintData;
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
 * Timing is driven by an injected `CompositorClock` (see Phase 3
 * sub-commit 1). The effect sets the clock once at construction time;
 * every animation reads from it for frame-perfect progression.
 */
class WindowAnimator : public PhosphorAnimation::AnimationController<KWin::EffectWindow*>
{
public:
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
};

} // namespace PlasmaZones
