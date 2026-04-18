// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/WindowMotion.h>

#include <QObject>
#include <QHash>

namespace KWin {
class EffectWindow;
class WindowPaintData;
}

namespace PlasmaZones {

/**
 * @brief Manages window snap animations (translate + scale)
 *
 * When a window is snapped to a zone, the caller applies moveResize()
 * immediately to set the final geometry. This animator provides visual
 * translation and scale transforms in paintWindow() that morph the
 * window from its old position/size to the new one. This follows the
 * standard KDE effect pattern — effects are purely visual overlays on
 * the compositing pipeline and never call moveResize() per-frame.
 *
 * Timing is driven by presentTime (vsync-aligned) for frame-perfect
 * animation, and progress is cached once per frame to ensure consistent
 * position and size interpolation within a single paint cycle.
 */
class WindowAnimator : public QObject
{
    Q_OBJECT

public:
    explicit WindowAnimator(QObject* parent = nullptr);

    // Configuration
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
    void setEasingCurve(const PhosphorAnimation::Easing& curve)
    {
        m_easing = curve;
    }
    const PhosphorAnimation::Easing& easingCurve() const
    {
        return m_easing;
    }
    void setMinDistance(int pixels)
    {
        m_minDistance = qMax(0, pixels);
    }
    int minDistance() const
    {
        return m_minDistance;
    }

    // Animation management
    bool hasActiveAnimations() const
    {
        return !m_animations.isEmpty();
    }
    bool hasAnimation(KWin::EffectWindow* window) const;
    bool startAnimation(KWin::EffectWindow* window, const QPointF& oldPosition, const QSizeF& oldSize,
                        const QRect& targetGeometry);
    void removeAnimation(KWin::EffectWindow* window);
    void clear();

    // Animation state queries
    bool isAnimatingToTarget(KWin::EffectWindow* window, const QRect& targetGeometry) const;

    /// Current visual top-left position (lerped between start and target).
    /// Used to chain animations when redirecting to a new target mid-flight.
    QPointF currentVisualPosition(KWin::EffectWindow* window) const;

    /// Current visual size (lerped between start and target).
    /// Used to chain animations when redirecting to a new target mid-flight.
    QSizeF currentVisualSize(KWin::EffectWindow* window) const;

    // Per-frame: update cached progress and clean up completed animations.
    // Called from prePaintScreen() with the compositor's presentTime.
    void advanceAnimations(std::chrono::milliseconds presentTime);

    // Schedule targeted repaints for active animation regions. Called from postPaintScreen().
    void scheduleRepaints() const;

    // Paint helper — applies translate offset and scale transform.
    // The window visually morphs from startPosition/startSize to its final geometry.
    void applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const;

    // Bounding rect covering the full animation path (for repaint regions)
    QRectF animationBounds(KWin::EffectWindow* window) const;

private:
    QHash<KWin::EffectWindow*, PhosphorAnimation::WindowMotion> m_animations;
    bool m_enabled = true;
    qreal m_duration = 150.0;
    PhosphorAnimation::Easing m_easing;
    int m_minDistance = 0;
};

} // namespace PlasmaZones
