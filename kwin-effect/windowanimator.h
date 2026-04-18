// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimationController.h>

namespace KWin {
class EffectWindow;
class WindowPaintData;
}

namespace PlasmaZones {

/**
 * @brief KWin adapter on top of `PhosphorAnimation::AnimationController`.
 *
 * The compositor-agnostic state machine (lifecycle, progression, bounds
 * computation, completion + repaint hooks) lives in the library base.
 * This subclass binds the handle type to `KWin::EffectWindow*`,
 * routes the four virtual hooks into KWin's effect pipeline, and adds
 * `applyTransform` — the only KWin-coupled call still needed.
 *
 * When a window is snapped to a zone, the caller applies moveResize()
 * immediately to set the final geometry. The animator provides visual
 * translation and scale transforms in paintWindow() that morph the
 * window from its old position/size to the new one. This follows the
 * standard KDE effect pattern — effects are purely visual overlays on
 * the compositing pipeline and never call moveResize() per-frame.
 *
 * Timing is driven by presentTime (vsync-aligned) for frame-perfect
 * animation, and progress is cached once per frame to ensure consistent
 * position and size interpolation within a single paint cycle.
 */
class WindowAnimator : public PhosphorAnimation::AnimationController<KWin::EffectWindow*>
{
public:
    /// Apply translate offset + scale transform to @p data so the window
    /// renders at the cached visual position/size. No-op when @p window
    /// has no active animation. The KWin-specific paint hook — every
    /// other piece of the controller is compositor-agnostic.
    void applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const;

protected:
    void onAnimationStarted(KWin::EffectWindow* window, const PhosphorAnimation::WindowMotion& motion) override;
    void onAnimationComplete(KWin::EffectWindow* window, const PhosphorAnimation::WindowMotion& motion) override;
    void onRepaintNeeded(KWin::EffectWindow* window, const QRectF& bounds) const override;
    bool isHandleValid(KWin::EffectWindow* window) const override;
    QMarginsF expandedPadding(KWin::EffectWindow* window, const PhosphorAnimation::WindowMotion& motion) const override;
};

} // namespace PlasmaZones
